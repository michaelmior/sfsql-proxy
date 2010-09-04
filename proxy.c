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
#include <signal.h>

#include "proxy.h"

#define QUEUE_LENGTH  10

volatile sig_atomic_t run = 1;
static char BUF[BUFSIZ];

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
    struct sockaddr_in serveraddr, clientaddr;
    struct hostent *hostinfo;
    proxy_work_t *work;
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
    serveraddr.sin_family = AF_INET;

    /* Set the binding address */
    if (host) {
        hostinfo = gethostbyname(host);
        if (!hostinfo) {
            proxy_error("Invalid binding address %s\n", host);
            return;
        } else {
            serveraddr.sin_addr = *(struct in_addr*) hostinfo->h_addr;
        }
    } else {
        serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    serveraddr.sin_port = htons((unsigned short) port);

    /* Bind the socket and start accepting connections */
    if (bind(serverfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
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

        clientfd = accept(serverfd, (struct sockaddr*) &clientaddr, &clientlen);
        if (clientfd < 0) {
            if (errno != EINTR)
                proxy_error("Error accepting client connection: %s", errstr);
            continue;
        }

        /* Process the new client */
        work = (proxy_work_t*) malloc(sizeof(proxy_work_t));
        work->clientfd = clientfd;
        work->addr = &clientaddr;
        work->proxy = NULL;

        /* Pick a thread to execute the work */
        thread = &(threads[proxy_pool_get(thread_pool)]);

        /* Give work to thread and signal it to go */
        proxy_mutex_lock(&(thread->lock));
        thread->work = work;
        proxy_cond_signal(&(thread->cv));
        proxy_mutex_unlock(&(thread->lock));
    }

    close(serverfd);
}

/**
 *  Cancel all running client threads. This signals threads to check
 *  for work, and they will exit upon seeing no work available.
 *  We then return any lock threads to the pool.
 **/
static void cancel_threads() {
    int i;
    
    for (i=0; i<PROXY_THREADS; i++) {
        /* Make sure worker threads release their mutex */
        proxy_mutex_lock(&(threads[i].lock));
        proxy_cond_signal(&(threads[i].cv));
        proxy_mutex_unlock(&(threads[i].lock));

        /* Try to acquire the lock again to ensure threads
         * have exited and cleaned up */
        proxy_mutex_lock(&(threads[i].lock));
        proxy_mutex_unlock(&(threads[i].lock));
    }

    /* Return any locked threads to the pool */
    while ((i = proxy_pool_get_locked(thread_pool)) >= 0)
        proxy_pool_return(thread_pool, i);
}

/**
 * Main signal handler with switch() to handle multiple signals.
 **/
static void catch_sig(int sig) {
    switch (sig) {
        /* Tell the server to stop */
        case SIGINT:
            /* Stop the server loop */
            run = 0;

            /* Cancel running threads */
            cancel_threads();

            break;
        case SIGUSR1:
            proxy_backends_update();
            break;
    }
}


int main(int argc, char *argv[]) {
    int error, i, ret=EXIT_SUCCESS;
    pthread_attr_t attr;
    struct sigaction new_action, old_action;

    ret = parse_options(argc, argv);
    if (ret != EXIT_SUCCESS)
        goto out_free;

    /* Threading initialization */
    proxy_threading_init();

    /* Install signal handler */
    new_action.sa_handler = catch_sig;
    sigemptyset(&new_action.sa_mask);
    sigaddset(&new_action.sa_mask, SIGINT);
    sigaddset(&new_action.sa_mask, SIGUSR1);
    new_action.sa_flags = 0;

    /* Handle signal for stopping server */
    sigaction(SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction(SIGINT, &new_action, NULL);

    /* Handle signal for reloading backend file */
    sigaction(SIGUSR1, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction(SIGUSR1, &new_action, NULL);

    /* Initialize libmysql */
    mysql_library_init(0, NULL, NULL);

    /* Create a thread pool */
    thread_pool = proxy_pool_new(PROXY_THREADS);

    /* Set up thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    /* Create the new threads */
    for (i=0; i<PROXY_THREADS; i++) {
        threads[i].id = i;
        proxy_cond_init(&(threads[i].cv));
        proxy_mutex_init(&threads[i].lock);
        threads[i].work = NULL;

        pthread_create(&(threads[i].thread), &attr, proxy_net_new_thread, (void*) &(threads[i]));
    }

    /* Initialize backend data */
    proxy_backend_init();

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
    cancel_threads();
    printf("Threads canceled\n");

    proxy_pool_destroy(thread_pool);
    for (i=0; i<PROXY_THREADS; i++) {
        printf("Joining thread %d\n", i);
        pthread_join(threads[i].thread, NULL);

        proxy_cond_destroy(&(threads[i].cv));
        proxy_mutex_destroy(&(threads[i].lock));
    }

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

    return ret;
}
