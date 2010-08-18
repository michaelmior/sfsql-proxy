#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "proxy.h"

#define QUEUE_LENGTH 10

static MYSQL *mysql;

/* Output error message */
void proxy_error(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    fprintf(stderr, "\n");
}

void server_run() {
    int serverfd, clientfd, optval, error;
    unsigned int clientlen;
    struct sockaddr_in serveraddr, clientaddr;
    struct st_vio *vio_tmp;
    NET *net;

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
    serveraddr.sin_port = htons((unsigned short) PROXY_PORT);

    /* Bind the socket and start accepting connections */
    if (bind(serverfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0)
        proxy_error("Error binding server socket on port %d", PROXY_PORT);

    if (listen(serverfd, QUEUE_LENGTH) < 0)
        proxy_error("Error listening on server socket: %s", strerror(errno));

    /* Server event loop */
    clientlen = sizeof(clientaddr);
    while(1) {
        clientfd = accept(serverfd, (struct sockaddr*) &clientaddr, &clientlen);
        if (clientfd < 0) {
            proxy_error("Error accepting client connection: %s", strerror(errno));
            continue;
        }

        /* derived from sql/mysqld.cc:handle_connections_sockets */
        vio_tmp = vio_new(clientfd, VIO_TYPE_TCPIP, 0);
        vio_keepalive(vio_tmp, TRUE);

        /* Initialize the client network structure */
        net = &(mysql->net);
        my_net_init(net, vio_tmp);
        my_net_set_write_timeout(net, NET_WRITE_TIMEOUT);
        my_net_set_read_timeout(net, NET_READ_TIMEOUT);

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
    }
}

int main(int argc, char *argv[]) {
    int error;

    /* Initialize libmysql */
    mysql_library_init(0, NULL, NULL);
    mysql = mysql_init(NULL);
    if (mysql == NULL) {
        error = -1;
        proxy_error("Out of memory when allocating proxy server");
        goto out;
    }

    /* Initialize network structures */
    mysql->protocol_version = PROTOCOL_VERSION;
    mysql->server_version   = MYSQL_SERVER_VERSION;

    /* Connect to the backend server (default parameters for now) */
    if ((error = proxy_backend_connect(NULL, -1, NULL, NULL, NULL)))
        goto out;

    /* Start proxying */
    server_run();

    /* Shutdown */
out:
    proxy_backend_close();
    mysql_close(mysql);
    mysql_library_end();

    return EXIT_SUCCESS;
}
