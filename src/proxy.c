/******************************************************************************
 * proxy.c
 *
 * Main proxy executable and server setup
 *
 * Copyright (c) 2010, Michael Mior <mmior@cs.toronto.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

/**
 * \mainpage
 *
 * This documentation is intended for future developers to gain
 * a quick understanding of the code. Attempts have been to
 * document all functions and data structures.
 *
 * This software and associated documentation is currently maintained
 * by <a href="http://www.cs.toronto.edu/~mmior">Michael Mior</a>.
 **/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sysexits.h>

#include "proxy.h"

/** File to store PID of proxy process */
#define PID_FILE      "/var/run/sfsql-proxy.pid"

volatile sig_atomic_t run = 0;
/** PID of process which signaled to start cloning */
pid_t signaller = -1;

static void server_run(char *host, int port);
static inline void client_threads_start();

/**
 * Main server loop which accepts external extensions
 *
 * @param host Host which the listening socket should be bound to.
 * @param port The port number for listening to incoming connections.
 **/
static void server_run(char *host, int port) {
    int serverfd, clientfd;
    fd_set fds;
    unsigned int clientlen;
    union sockaddr_union clientaddr;
    proxy_thread_t *thread;

    /* Create and bind a new socket */
    if ((serverfd = proxy_net_bind_new_socket(host, port)) < 0)
        return;

    /* Server event loop */
    proxy_start_time = time(NULL);
    clientlen = sizeof(clientaddr);
    run = 1;
    while(run) {
        FD_ZERO(&fds);
        FD_SET(serverfd, &fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) != 1)
            continue;

        clientfd = accept(serverfd, &clientaddr.sa, &clientlen);
        if (clientfd < 0) {
            if (errno != EINTR)
                proxy_log(LOG_ERROR, "Error accepting client connection: %s", errstr);
            continue;
        }

        /* Pick a thread to execute the work */
        thread = &(net_threads[proxy_pool_get(thread_pool)]);

        /* Give work to thread and signal it to go */
        proxy_mutex_lock(&thread->lock);
        thread->data.work.clientfd = clientfd;
        thread->data.work.addr = &clientaddr.sin;
        thread->data.work.proxy = NULL;
        proxy_cond_signal(&thread->cv);
        proxy_mutex_unlock(&thread->lock);
    }

    /* Server is shutting down, close the listening socket */
    close(serverfd);
}

/**
 * Main signal handler with switch() to handle multiple signals.
 **/
static void catch_sig(int sig, __attribute__((unused)) siginfo_t *info, __attribute__((unused)) void *ucontext_t) {
    int i;

    switch (sig) {
        /* Tell the server to stop */
        case SIGINT:
        case SIGTERM:
            /* Stop the server loop */
            run = 0;

            /* Cancel running threads */
            for (i=0; i<options.client_threads; i++)
                pthread_kill(net_threads[i].thread, SIGPOLL);
            proxy_threading_cancel(net_threads, options.client_threads, thread_pool);

            break;

        /* Prepare to clone */
        case SIGUSR1:
            cloning = 1;

            if (signaller > 0)
                proxy_log(LOG_ERROR, "Received second cloning signal before clone complete");

            signaller = info->si_pid;

            /* Wait for queries to finish */
            while (querying) { usleep(1000); }

            /* Signal the process to clone */
            proxy_log(LOG_INFO, "Signaling back %d", signaller);
            kill(signaller, SIGUSR1);

            break;

        /* Update backends with new clone */
        case SIGUSR2:
            proxy_backends_update();
            cloning = 0;
            proxy_log(LOG_INFO, "Resuming queries after clone completion");

            if (info->si_pid != signaller)
                proxy_log(LOG_ERROR, "Different process sent cloning completion signal");
            signaller = -1;
            break;
    }
}

/**
 * Start threads to manage client connections.
 **/
static inline void client_threads_start() {
    pthread_attr_t attr;
    int i;

    /* Create a thread pool */
    thread_pool = proxy_pool_new(options.client_threads);

    /* Set up thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* Create the new threads */
    net_threads = (proxy_thread_t*) calloc(options.client_threads, sizeof(proxy_thread_t));

    for (i=0; i<options.client_threads; i++) {
        /* Initialize thread data */
        net_threads[i].id = i;
        proxy_cond_init(&net_threads[i].cv);
        proxy_mutex_init(&net_threads[i].lock);
        net_threads[i].exit = 0;
        net_threads[i].data.work.addr = NULL;
        net_threads[i].data.work.proxy = NULL;

        pthread_create(&net_threads[i].thread, &attr, proxy_net_new_thread, (void*) &net_threads[i]);
    }
}

int main(int argc, char *argv[]) {
    int error, ret=EX_OK;
    struct sigaction new_action;
    FILE *pid_file;
    pid_t pid;
    my_bool wrote_pid = FALSE;
    char *buf;

    /* Parse command-lne options */
    ret = proxy_options_parse(argc, argv);
    if (ret != EX_OK)
        goto out_free;

    /* Check for existing process */
    if (access(PID_FILE, F_OK) == 0) {
        fprintf(stderr, "PID file already exists in %s\n", PID_FILE);
        ret = EX_SOFTWARE;
        goto out_free;
    }

    /* Open the log file */
    if (proxy_log_open())
        goto out_free;

    /* Daemonize if necessary */
    if (options.daemonize) {
        if (daemon(1, 0)) {
            fprintf(stderr, "Couldn't daemonize: %s", strerror(errno));
            ret = EX_OSERR;
            goto out_free;
        }
    }

    /* Write PID file */
    pid = getpid();
    pid_file = fopen(PID_FILE, "w");

    if (!pid_file || (fprintf(pid_file, "%d\n", (int) pid) < 0)) {
        proxy_log(LOG_ERROR, "Couldn't write PID file");
    } else {
        wrote_pid = TRUE;
        fclose(pid_file);
    }

    /* Initialization */
    proxy_threading_init();
    buf = (char*) malloc(BUFSIZ);
    pthread_setspecific(thread_buf_key, buf);

    /* Install signal handler */
    new_action.sa_sigaction = catch_sig;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO;

#define HANDLE_SIG(sig) \
    sigaddset(&new_action.sa_mask, sig); \
    sigaction(sig, &new_action, NULL);

    /* Set up signal handlers */
    HANDLE_SIG(SIGTERM);
    HANDLE_SIG(SIGINT);
    HANDLE_SIG(SIGUSR1);
    HANDLE_SIG(SIGUSR2);
    HANDLE_SIG(SIGPOLL);

    /* Initialize libmysql */
    mysql_library_init(0, NULL, NULL);

    /* Start threads to handle clients */
    client_threads_start();

    /* Initialize backend data */
    if (proxy_backend_init()) {
        ret = EX_SOFTWARE;
        goto out;
    }

    /* Connect to the backend server */
    if (options.backend_file)
        error = proxy_backends_connect();
    else
        error = proxy_backend_connect();

    if (error) {
        ret = EX_SOFTWARE;
        goto out;
    }

    /* Initialize global status */
    global_connections = 0;
    global_status.bytes_recv = 0;
    global_status.bytes_sent = 0;
    global_status.queries = 0;

    /* Prepare monitoring */
    proxy_monitor_init();

    /* Set up transaction and cloning data */
    proxy_trans_init();
    proxy_clone_init();

    /* Start proxying */
    proxy_log(LOG_INFO, "Starting proxy on %s:%d",
        options.phost[0] != '\0' ? options.phost : "0.0.0.0", options.pport);
    server_run(options.phost[0] != '\0' ? options.phost : NULL, options.pport);

    /* Shutdown */
out:
    proxy_log(LOG_INFO, "Shutting down...");

    /* Cancel any outstanding client threads */
    proxy_threading_cancel(net_threads, options.client_threads, thread_pool);
    proxy_threading_cleanup(net_threads, options.client_threads, thread_pool);

    proxy_backend_close();
    proxy_trans_end();
    proxy_clone_end();
    mysql_library_end();
    proxy_threading_end();

out_free:
    /* Delete PID file */
    if (wrote_pid && unlink(PID_FILE))
        proxy_log(LOG_ERROR, "Can't remove PID file: %s", errstr);

    proxy_log_close();

    return ret;
}
