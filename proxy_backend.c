#include "proxy.h"

#include <sql_common.h>
#include <client_settings.h>

#define BACKEND_HOST "127.0.0.1"
#define BACKEND_PORT 3306
#define BACKEND_USER "root"
#define BACKEND_PASS "root"
#define BACKEND_DB   "test"

#define MAX_PACKET_LENGTH (256L*256L*256L-1)

static MYSQL *mysql_backend = NULL;

static my_bool backend_read_rows(MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *proxy);

/* derived from sql/client.c:cli_safe_read */
static ulong backend_read_to_proxy(MYSQL *proxy) {
    NET *net = &(mysql_backend->net);
    ulong pkt_len;

    if (unlikely(!net->vio))
        return (packet_error);

    pkt_len = my_net_read(net);

    /* XXX: need to return error to client */
    if (pkt_len == packet_error || pkt_len == 0) {
        if (! (net->vio && vio_was_interrupted(net->vio))) {
            /* XXX: fatal error, close down 
             * should give soft error to client and reconnect with backend */
            proxy_error("Interrupted when reading backend response");
        } else
            proxy_error("Received error from backend");

        return (packet_error);
    }

    /* XXX: could probably generate soft error
     * for client in below case */
    if (net->read_pos[0] == 255 && pkt_len <= 3)
        return (packet_error);

    /* Read from the backend and forward to the proxy connection */
    if (my_net_write(&(proxy->net), net->read_pos, (size_t) pkt_len)) {
        proxy_error("Couldn't forward backend packet to proxy");
        return (packet_error);
    }

    /* Flush the write buffer */
    net_flush(&(proxy->net));

    return pkt_len;
}

/* derived from sql/client.c:cli_read_rows */
static my_bool backend_read_rows(MYSQL *proxy, uint fields) {
    uchar *cp;
    uint field;
    ulong pkt_len = 8, len;

    /* Read until EOF (254) marker reached */
    while (*(cp = mysql_backend->net.read_pos) != 254 || pkt_len >= 8) {
        for (field=0; field<fields; field++) {
            len = (ulong) net_field_length(&cp);

            /* skip over size of field */
            if (len != NULL_LENGTH)
                cp += len;
        }

        /* Read and forward the row to the proxy */
        if ((pkt_len = backend_read_to_proxy(proxy)) == packet_error)
            return TRUE;
    }

    /* success */
    return FALSE;
}

int proxy_backend_connect(char *host, int port, char *user, char *pass, char *db) {
    mysql_backend = mysql_init(NULL);
    if (mysql_backend == NULL) {
        proxy_error("Out of memory when allocating MySQL backend");
        return -1;
    }

    /* Set default parameters
     * use empty strings to specify NULL */
    if (!host)
        host = BACKEND_HOST;
    if (port < 0)
        port = BACKEND_PORT;
    if (!user)
        user = BACKEND_USER;
    else if (*user == '\0')
        user = NULL;
    if (!pass)
        pass = BACKEND_PASS;
    else if (*pass == '\0')
        pass = NULL;
    if (!db)
        db = BACKEND_DB;
    else if (*db == '\0')
        db = NULL;

    if (!mysql_real_connect(mysql_backend,
                host, user, pass, db, port, NULL, 0)) {
        proxy_error("Failed to connect to MySQL backend: %s",
                mysql_error(mysql_backend));
        return -1;
    }

    return 0;
}

my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    my_bool error = FALSE, i;
    ulong pkt_len = 8;
    uchar *pos;

    /* XXX: need to sync with proxy? */
    printf("Sending query %s to backend\n", query);
    mysql_send_query(mysql_backend, query, length);

    /* derived from sql/client.c:cli_read_query_result */
    /* read info and result header packets */
    for (i=0; i<2; i++) {
        if ((pkt_len = backend_read_to_proxy(proxy)) == packet_error)
            return TRUE;

        /* If the query doesn't return results, no more to do */
        pos = (uchar*) mysql_backend->net.read_pos;
        if (net_field_length(&pos) == 0 || mysql_backend->net.read_pos[0] == 255) {
            error = FALSE;
            goto out; 
        }
    }

    /* read field info */
    if (backend_read_rows(proxy, 7))
        return TRUE;

    /* Read result rows
     *
     * Here we assume the client has called mysql_store_result()
     * and wishes to retrieve all rows immediately. Otherwise,
     * the backend would be tied up waiting for the client to
     * decide when to fetch rows. (Clients using mysql_use_result()
     * should still function, but with possible network overhead.
     * */
    if (backend_read_rows(proxy, mysql_backend->field_count))
        return TRUE;

out:
    /* Flush the write buffer */
    return error;
}

/* Close the open connection to the backend */
void proxy_backend_close() {
    mysql_close(mysql_backend);
}
