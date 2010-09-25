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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "proxy.h"

#define QUEUE_LENGTH  10

volatile sig_atomic_t run = 1;
volatile sig_atomic_t cloning  = 1;
static proxy_thread_t *threads;
pid_t signaller = -1;
static char BUF[BUFSIZ];

/** Union used to avoid aliasing warnings. */
union sockaddr_union {
    /** Standard socket structure. */
    struct sockaddr sa;
    /** Incoming socket structure. */
    struct sockaddr_in sin;
};

static void server_run(char *host, int port);

/**
 * Main server loop which accepts external extensions
 *
 * \param host Host which the listening socket should be bound to.
 * \param port The port number for listening to incoming connections.
 **/
static void server_run(char *host, int port) {
    int serverfd, clientfd;
    fd_set fds;
    unsigned int clientlen;
    union sockaddr_union serveraddr, clientaddr;
    struct hostent *hostinfo;
    proxy_thread_t *thread;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);

    /* If we're debugging, allow reuse of the socket */
#ifdef DEBUG
    {
        int optval = 1;
        setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (const void*) &optval, sizeof(int));
    }
#endif

    /* Intialize the server address */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin.sin_family = AF_INET;

    /* Set the binding address */
    if (host) {
        hostinfo = gethostbyname(host);
        if (!hostinfo) {
            proxy_error("Invalid binding address %s\n", host);
            return;
        } else {
            serveraddr.sin.sin_addr = *(struct in_addr*) hostinfo->h_addr;
        }
    } else {
        serveraddr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    serveraddr.sin.sin_port = htons((unsigned short) port);

    /* Bind the socket and start accepting connections */
    if (bind(serverfd, &(serveraddr.sa), sizeof(serveraddr)) < 0) {
        proxy_error("Error binding server socket on port %d", port);
        return;
    }

    if (listen(serverfd, QUEUE_LENGTH) < 0) {
        proxy_error("Error listening on server socket: %s", errstr);
        return;
    }

    /* Server event loop */
    clientlen = sizeof(clientaddr);
    while(run) {
        FD_ZERO(&fds);
        FD_SET(serverfd, &fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) != 1)
            continue;

        clientfd = accept(serverfd, &(clientaddr.sa), &clientlen);
        if (clientfd < 0) {
            if (errno != EINTR)
                proxy_error("Error accepting client connection: %s", errstr);
            continue;
        }

        /* Pick a thread to execute the work */
        thread = &(threads[proxy_pool_get(thread_pool)]);

        /* Give work to thread and signal it to go */
        proxy_mutex_lock(&(thread->lock));
        thread->data.work.clientfd = clientfd;
        thread->data.work.addr = &(clientaddr.sin);
        thread->data.work.proxy = NULL;
        proxy_cond_signal(&(thread->cv));
        proxy_mutex_unlock(&(thread->lock));
    }

    close(serverfd);
}

/**
 * Main signal handler with switch() to handle multiple signals.
 **/
static void catch_sig(int sig, __attribute__((unused)) siginfo_t *info, __attribute__((unused)) void *ucontext_t) {
    switch (sig) {
        /* Tell the server to stop */
        case SIGINT:
            /* Stop the server loop */
            run = 0;

            /* Cancel running threads */
            proxy_threading_cancel(threads, options.client_threads, thread_pool);

            break;

        /* Prepare to clone */
        case SIGUSR1:
            cloning = 1;

            if (signaller > 0)
                proxy_error("Received second cloning signal before clone complete");

            signaller = info->si_pid;

            /* Wait for queries to finish */
            while (querying) { usleep(1000); }

            /* Signal the process to clone */
            printf("Signaling back %d\n", signaller);
            kill(signaller, SIGUSR1);

            break;

        /* Update backends with new clone */
        case SIGUSR2:
            proxy_backends_update();
            cloning = 0;

            if (info->si_pid != signaller)
                proxy_error("Different process sent cloning completion signal");
            signaller = -1;
            break;
    }
}

int main(int argc, char *argv[]) {
    int error, i, ret=EXIT_SUCCESS;
    pthread_attr_t attr;
    struct sigaction new_action, old_action;

    ret = parse_options(argc, argv);
    if (ret != EXIT_SUCCESS || options.help)
        goto out_free;

    /* Threading initialization */
    proxy_threading_init();

    /* Install signal handler */
    new_action.sa_sigaction = catch_sig;
    sigemptyset(&new_action.sa_mask);
    sigaddset(&new_action.sa_mask, SIGINT);
    sigaddset(&new_action.sa_mask, SIGUSR1);
    new_action.sa_flags = SA_SIGINFO;

#define HANDLE_SIG(sig) \
    sigaction(sig, NULL, &old_action); \
    if (old_action.sa_handler != SIG_IGN) \
        sigaction(sig, &new_action, NULL); \
    sigaction(sig, NULL, &old_action);

    /* Set up signal handlers */
    HANDLE_SIG(SIGTERM);
    HANDLE_SIG(SIGINT);
    HANDLE_SIG(SIGUSR1);
    HANDLE_SIG(SIGUSR2);

    /* Initialize libmysql */
    mysql_library_init(0, NULL, NULL);

    /* Create a thread pool */
    thread_pool = proxy_pool_new(options.client_threads);

    /* Set up thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* Create the new threads */
    threads = (proxy_thread_t*) calloc(options.client_threads, sizeof(proxy_thread_t));

    for (i=0; i<options.client_threads; i++) {
        threads[i].id = i;
        proxy_cond_init(&(threads[i].cv));
        proxy_mutex_init(&(threads[i].lock));
        threads[i].data.work.proxy = NULL;

        pthread_create(&(threads[i].thread), &attr, proxy_net_new_thread, (void*) &(threads[i]));
    }

    /* Initialize backend data */
    if (proxy_backend_init()) {
        ret = EXIT_FAILURE;
        goto out;
    }

    /* Connect to the backend server */
    if (options.backend_file)
        error = proxy_backends_connect();
    else
        error = proxy_backend_connect();

    if (error) {
        ret = EXIT_FAILURE;
        goto out;
    }

    /* Start proxying */
    printf("Starting proxy on %s:%d\n",
        options.phost ? options.phost : "0.0.0.0", options.pport);
    server_run(options.phost, options.pport);

    /* Shutdown */
out:
    printf("Shutting down...\n");

    /* Cancel any outstanding client threads */
    proxy_threading_cancel(threads, options.client_threads, thread_pool);
    proxy_threading_cleanup(threads, options.client_threads, thread_pool);

    proxy_backend_close();
    mysql_library_end();
    proxy_threading_end();

out_free:
    /* Free additional memory */
    free(options.backend.host);
    free(options.user);
    free(options.pass);
    free(options.db);
    free(options.backend_file);
    free(options.phost);
    free(options.mapper);

    return ret;
}
