#include <my_global.h>
#include <mysql_com.h>
#include <mysql.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "check_net.h"
#include "../proxy.h"

MYSQL* client_init(Vio *vio);
my_bool __real_my_net_init(NET *net, Vio *vio);

my_bool __wrap_my_net_init(NET *net, Vio *vio) {
    struct sockaddr_un sa;
    int s;

    /* Connect to the local socket */
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCK_NAME, sizeof(struct sockaddr_un) - sizeof(short));

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0 || connect(s, (struct sockaddr*) &sa, sizeof(struct sockaddr_un)))
        return TRUE;

    /* Finish structure setup */
    vio = vio_new(s, VIO_TYPE_SOCKET, 0);
    vio->localhost = TRUE;
    __real_my_net_init(net, vio);

    return FALSE;
}

my_bool __wrap_proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    /* For now, this function does nothing.
     * In the future, we can check that
     * client queries are properly received. */
    printf("Received query: %s\n", query);
    return FALSE;
}

/* Don't need to touch the pool here */
void __wrap_proxy_return_to_pool(pool_t *pool, int idx) {}
