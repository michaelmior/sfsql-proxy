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
#include <getopt.h>
#include <sysexits.h>

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

/** Print a simple usage message with command-line arguments.
 **/
static void usage() {
    printf(
            "SnowFlock SQL proxy server - (C) Michael Mior <mmior@cs.toronto.edu>, 2010\n\n"
            "Options:\n"
            "\t--help,         -?\tShow this message\n\n"
            "Backend options:\n"
            "\t--backend-host, -h\tHost to forward queries to (default: 127.0.0.1)\n"
            "\t--backend-port, -p\tPort of the backend host (default: 3306)\n"
            "\t--backend-db,   -D\tName of database on the backend (default: test)\n"
            "\t--backend-user, -u\tUser for backend server (default: root)\n"
            "\t--backend-pass, -p\tPassword for backend user\n"
            "\t--backend-file, -f\tFile listing available backends\n"
            "\t                  \t(cannot be specified with above options)\n"
            "\t--num-backends, -N\tNumber connections per backend\n"
            "\t                -a\tDisable autocommit (default is enabled)\n\n"
            "Proxy options:\n"
            "\t--proxy-host,   -b\tBinding address (default is 0.0.0.0)\n"
            "\t--proxy-port,   -L\tPort for the proxy server to listen on (default: 4040)\n"
    );
}

int main(int argc, char *argv[]) {
    int error, pport, c, i, num_backends=NUM_BACKENDS, ret=EXIT_SUCCESS;
    proxy_backend_t backend;
    my_bool autocommit = TRUE;
    char *user, *pass, *db, *phost, *backend_file;
    pthread_attr_t attr;
    struct sigaction new_action, old_action;

    /* Set arguments to default values */
    backend.host = NULL;
    backend.port = 0;
    user = pass = db = phost = backend_file = NULL;
    pport = PROXY_PORT;

    /* Parse command-line options */
    while(1) {
        static struct option long_options[] = {
            {"help",         no_argument,       0, '?'},
            {"backend-host", required_argument, 0, 'h'},
            {"backend-port", required_argument, 0, 'P'},
            {"backend-db",   required_argument, 0, 'D'},
            {"backend-user", required_argument, 0, 'u'},
            {"backend-pass", required_argument, 0, 'p'},
            {"backend-file", required_argument, 0, 'f'},
            {"num-backends", required_argument, 0, 'N'},
            {"proxy-host",   required_argument, 0, 'b'},
            {"proxy-port",   required_argument, 0, 'L'},
            {0, 0, 0, 0}
        };

        int opt = 0;
        c = getopt_long(argc, argv, "?h:P:D:u:p:f:N:aAb:L:", long_options, &opt);

        if (c == -1)
            break;

        switch(c) {
            case '?':
                usage();
                ret = EXIT_SUCCESS;
                goto out_free;
            case 'h':
                backend.host = strdup(optarg);
                break;
            case 'P':
                backend.port = atoi(optarg);
                break;
            case 'D':
                db = strdup(optarg);
                break;
            case 'u':
                user = strdup(optarg);
                break;
            case 'p':
                pass = strdup(optarg);
                break;
            case 'f':
                backend_file = strdup(optarg);
                break;
            case 'N':
                num_backends = atoi(optarg);
                break;
            case 'a':
                autocommit = FALSE;
                break;
            case 'b':
                phost = strdup(optarg);
                break;
            case 'L':
                pport = atoi(optarg);
                break;
            default:
                usage();
                ret = EX_USAGE;
                goto out_free;
        }
    }

    if (backend_file) {
        if (backend.host || backend.port > 0) {
            usage();
            ret = EX_USAGE;
            goto out_free;
        } else if (access(backend_file, R_OK)) {
            proxy_error("Error accessing backend file:%s", errstr);
            ret = EX_NOINPUT;
            goto out_free;
        }
    }

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

    /* Connect to the backend server (default parameters for now) */
    if (backend_file) {
        error = proxy_backends_connect(backend_file,
                user ? user : BACKEND_USER,
                pass ? pass : BACKEND_PASS,
                db   ? db   : BACKEND_DB,
                autocommit);
    } else {
        backend.host = backend.host ? backend.host : strdup(BACKEND_HOST);
        backend.port = backend.port ? backend.port : BACKEND_PORT;
        error = proxy_backend_connect(&backend,
                user ? user : BACKEND_USER,
                pass ? pass : BACKEND_PASS,
                db   ? db   : BACKEND_DB,
                num_backends, autocommit);
    }
    if (error) {
        ret = EXIT_FAILURE;
        goto out;
    }

    /* Start proxying */
    printf("Starting proxy on %s:%d\n", phost ? phost : "0.0.0.0", pport);
    server_run(phost, pport);

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
    free(backend.host);
    free(user);
    free(pass);
    free(db);
    free(backend_file);
    free(phost);

    return ret;
}
