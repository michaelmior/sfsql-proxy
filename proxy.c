#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <getopt.h>

#include "proxy.h"

#define QUEUE_LENGTH  10
#define PROXY_THREADS 10

volatile sig_atomic_t run = 1;
static pool_t *thread_pool;
static pthread_t threads[PROXY_THREADS];

/* Output error message */
void proxy_error(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    fprintf(stderr, "\n");
}

static void server_run(int port) {
    int serverfd, clientfd, optval;
    fd_set fds;
    unsigned int clientlen;
    struct sockaddr_in serveraddr, clientaddr;
    proxy_thread_t *thread;
    pthread_attr_t attr;
    pthread_t *client_thread;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);

    /* If we're debugging, allow reuse of the socket */
#ifdef DEBUG
    optval = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (const void*) &optval, sizeof(int));
#endif

    /* Intialize the server address */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); //TODO: allow specification of binding address
    serveraddr.sin_port = htons((unsigned short) port);

    /* Bind the socket and start accepting connections */
    if (bind(serverfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
        proxy_error("Error binding server socket on port %d", port);
        return;
    }

    if (listen(serverfd, QUEUE_LENGTH) < 0) {
        proxy_error("Error listening on server socket: %s", strerror(errno));
        return;
    }

    /* Set up thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

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
                proxy_error("Error accepting client connection: %s", strerror(errno));
            continue;
        }

        /* Process the new client */
        thread = (proxy_thread_t*) malloc(sizeof(proxy_thread_t));
        thread->clientfd = clientfd;
        thread->addr = &clientaddr;
        thread->id = proxy_get_from_pool(thread_pool);
        thread->proxy = NULL;
        client_thread = &(threads[thread->id]);
        printf("Creating thread with id %d\n", thread->id);
        pthread_create(client_thread, &attr, proxy_new_client, (void*) thread);
    }

    close(serverfd);
}

/* Cancel all running client threads */
static void cancel_threads() {
    int id;
    
    while ((id = proxy_pool_get_locked(thread_pool)) >= 0) {
        printf("Canceling thread %d\n", id);
        pthread_cancel(threads[id]);
        pthread_join(threads[id], NULL);
        proxy_return_to_pool(thread_pool, id);
        printf("Thread %d canceled and joined\n", id);
    }
}

static void catch_sig(int sig) {
    switch (sig) {
        /* Tell the server to stop */
        case SIGINT:
            /* Stop the server loop */
            run = 0;

            /* Canel running threads */
            cancel_threads();

            break;
    }
}

static void usage() {
    printf(
            "SnowFlock SQL proxy server - (C) Michael Mior <mmior@cs.toronto.edu>, 2010\n\n"
            "Options:\n"
            "\t--help,         -?\tShow this message\n"
            "\t--backend-host, -h\tHost to forward queries to\n"
            "\t--backend-port, -p\tPort of the backend host\n"
            "\t--backend-db,   -D\tName of database on the backend\n"
            "\t--backend-user, -u\tUser for backend server\n"
            "\t--backend-pass, -p\tPassword for backend user\n\n"
            "\t--proxy-port, -L\tPort for the proxy server to listen on\n"
    );
}

int main(int argc, char *argv[]) {
    int error, bport, pport, c;
    char *host, *db, *user, *pass;

    /* Set arguments to default values */
    host =  BACKEND_HOST;
    db =    BACKEND_DB;
    user =  BACKEND_USER;
    pass =  BACKEND_PASS;
    bport = BACKEND_PORT;
    pport = PROXY_PORT;

    /* Parse command-line options */
    while(1) {
        static struct option long_options[] = {
            {"help",         optional_argument, 0, '?'},
            {"backend-host", optional_argument, 0, 'h'},
            {"backend-port", optional_argument, 0, 'P'},
            {"backend-db",   optional_argument, 0, 'D'},
            {"backend-user", optional_argument, 0, 'u'},
            {"backend-pass", optional_argument, 0, 'p'},
            {"proxy-port",   optional_argument, 0, 'L'},
            {0, 0, 0, 0}
        };

        int opt = 0;
        c = getopt_long(argc, argv, "?h:P:D:u:p:L:", long_options, &opt);

        if (c == -1)
            break;

        switch(c) {
            case '?':
                usage();
                return EXIT_FAILURE;
            case 'h':
                host = strdup(optarg);
                break;
            case 'P':
                bport = atoi(optarg);
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
            case 'L':
                pport = atoi(optarg);
                break;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }

    /* Install signal handler */
    signal(SIGINT, catch_sig);

    /* Initialize libmysql */
    mysql_library_init(0, NULL, NULL);

    /* Create a thread pool */
    thread_pool = proxy_pool_new(PROXY_THREADS);

    /* Connect to the backend server (default parameters for now) */
    if ((error = proxy_backend_connect(host, bport, user, pass, db)))
        goto out;

    /* Start proxying */
    printf("Starting proxy on port %d\n", pport);
    server_run(pport);

    /* Shutdown */
out:
    printf("Shutting down...\n");

    /* Cancel any outstanding client threads */
    cancel_threads();

    proxy_backend_close();
    mysql_library_end();

    return EXIT_SUCCESS;
}
