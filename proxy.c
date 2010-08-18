#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <getopt.h>

#include "proxy.h"

#define QUEUE_LENGTH 10

volatile sig_atomic_t run = 1;

/* Output error message */
void proxy_error(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    fprintf(stderr, "\n");
}

MYSQL* proxy_init(Vio *vio) {
    MYSQL *mysql;
    NET *net;

    /* Initialize a MySQL object */
    mysql = mysql_init(NULL);
    if (mysql == NULL) {
        proxy_error("Out of memory when allocating proxy server");
        return NULL;
    }

    /* Initialize network structures */
    mysql->protocol_version = PROTOCOL_VERSION;
    mysql->server_version   = MYSQL_SERVER_VERSION;

    /* Initialize the client network structure */
    net = &(mysql->net);
    my_net_init(net, vio);
    my_net_set_write_timeout(net, NET_WRITE_TIMEOUT);
    my_net_set_read_timeout(net, NET_READ_TIMEOUT);

    return mysql;
}

void server_run(int port) {
    int serverfd, clientfd, optval, error;
    fd_set fds;
    unsigned int clientlen;
    struct sockaddr_in serveraddr, clientaddr;
    Vio *vio_tmp;
    MYSQL *mysql;

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

        /* derived from sql/mysqld.cc:handle_connections_sockets */
        vio_tmp = vio_new(clientfd, VIO_TYPE_TCPIP, 0);
        vio_keepalive(vio_tmp, TRUE);

        mysql = proxy_init(vio_tmp);
        if (mysql == NULL)
            goto error;

        /* Perform "authentication" (credentials not checked) */
        proxy_handshake(mysql, &clientaddr, 0);

        /* from sql/sql_connect.cc:handle_one_connection */
        while (!mysql->net.error && mysql->net.vio != 0) {
            error = proxy_read_query(mysql);
            if (error != 0) {
                if (error < 0)
                    proxy_error("Error in processing client query, disconnecting");
                break;
            }
        }

        /* XXX: may need to send error before closing connection */
        /* derived from sql/sql_mysqld.cc:close_connection */
        if (vio_close(mysql->net.vio) < 0)
            proxy_error("Error closing client connection: %s", strerror(errno));

error:
        /* Clean up data structures */
        vio_delete(mysql->net.vio);
        mysql->net.vio = 0;
        mysql_close(mysql);
    }
}

void catch_sig(int sig) {
    switch (sig) {
        /* Tell the server to stop */
        case SIGINT:
            run = 0;
            break;
    }
}

void usage() {
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

    /* Connect to the backend server (default parameters for now) */
    if ((error = proxy_backend_connect(host, bport, user, pass, db)))
        goto out;

    /* Start proxying */
    printf("Starting proxy on port %d\n", pport);
    server_run(pport);

    /* Shutdown */
out:
    printf("Shutting down...\n");
    proxy_backend_close();
    mysql_library_end();

    return EXIT_SUCCESS;
}
