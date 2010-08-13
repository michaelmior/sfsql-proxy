#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "proxy.h"

#define QUEUE_LENGTH 10

MYSQL *mysql;

/* Output error message */
void proxy_error(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    fprintf(stderr, "\n");

    exit(1);
}

void server_run() {
    int serverfd, clientfd, optval;
    unsigned int clientlen;
    struct sockaddr_in serveraddr, clientaddr;
    //struct hostent *client_host;
    struct st_vio *vio_tmp;

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
        proxy_error("Error listening on server socket");

    /* Server event loop */
    clientlen = sizeof(clientaddr);
    while(1) {
        clientfd = accept(serverfd, (struct sockaddr*) &clientaddr, &clientlen);

        /* derived from sql/mysqld.cc:handle_connections_sockets */
        vio_tmp = vio_new(clientfd, VIO_TYPE_TCPIP, 0);
        vio_keepalive(vio_tmp, TRUE);
        my_net_init(&(mysql->net), vio_tmp);
        proxy_handshake(&clientaddr, 0);

        close(clientfd);
    }
}

int main(int argc, char *argv[]) {
    mysql_library_init(0, NULL, NULL);
    
    mysql = mysql_init(NULL);

    /* Initialize network structures */
    mysql->protocol_version = PROTOCOL_VERSION;
    mysql->server_version = MYSQL_SERVER_VERSION;

    server_run();

    mysql_close(mysql);

    mysql_library_end();
    return EXIT_SUCCESS;
}
