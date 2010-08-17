#include "proxy.h"

#include <sql_common.h>
#include <client_settings.h>

#define BACKEND_HOST "127.0.0.1"
#define BACKEND_PORT 3306
#define BACKEND_USER "root"
#define BACKEND_PASS "root"
#define BACKEND_DB   "test"

static MYSQL *mysql_backend = NULL;

static my_bool backend_read_rows(MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *proxy);

/* derived from sql/client.c:cli_safe_read */
static ulong backend_read_to_proxy(MYSQL *proxy) {
    NET *net = &(mysql_backend->net);
    ulong pkt_len;

    printf("Starting packet forwarding (vio is %p)\n", net->vio);

    if (unlikely(!net->vio))
        return (packet_error);

    pkt_len = my_net_read(net);
    printf("Read %lu bytes from backend\n", (unsigned long) pkt_len);

    /* XXX: need to return error to client */
    if (pkt_len == packet_error || pkt_len == 0) {
        if (! (net->vio && vio_was_interrupted(net->vio))) {
            /* XXX: fatal error, close down 
             * should give soft error to client and reconnect with backend */
            proxy_error("Interrupted when reading backend response");
        }
        return (packet_error);
    }

    printf("Read %lu bytes from backend\n", (unsigned long) pkt_len);

    if (net->read_pos[0] == 255) {
        /* XXX: need to return error to client */
        if (pkt_len <= 3)
            return (packet_error);
    }

    /* Read from the backend and forward to the proxy connection */
    printf("backend packet:%d, proxy packet: %d, socket: %d\n", (int) net->pkt_nr, proxy->net.pkt_nr, proxy->net.vio->sd);
    if (my_net_write(&(proxy->net), net->read_pos, (size_t) pkt_len)) {
        proxy_error("Couldn't forward backend packet to proxy");
        return (packet_error);
    }

#if 0
    {
        /* Let's try a manual write */
        uchar buf[NET_HEADER_SIZE];
        int c, sd;
        int3store(buf, pkt_len);
        buf[3] = (uchar) net->pkt_nr;

        sd = proxy->net.vio->sd;

        printf("packet:%d, socket: %d\n", (int) net->pkt_nr, sd);
        
        if ((c = write(sd, buf, NET_HEADER_SIZE)) < 0) {
            proxy_error("%s", strerror(errno));
        } else {
            printf("Wrote %d bytes\n", c);
        }
        if ((c = write(sd, net->read_pos, pkt_len)) < 0) {
            proxy_error("%s", strerror(errno));
        } else {
            printf("Wrote %d bytes\n", c);
        }
    }
#endif

    printf("Wrote to proxy\n");

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

int proxy_backend_connect() {
    mysql_backend = mysql_init(NULL);
    if (mysql_backend == NULL)
        proxy_error("Out of memory when allocating MySQL backend");

    if (!mysql_real_connect(mysql_backend,
                BACKEND_HOST, BACKEND_USER, BACKEND_PASS,
                BACKEND_DB, BACKEND_PORT, NULL, 0)) {
        proxy_error("Failed to connect to MySQL backend: %s",
                mysql_error(mysql_backend));
        return 1;
    }

    return 0;
}

my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    uint error = 0, i;
    ulong pkt_len = 8; //, field_count;
    //uchar *pos;
    //MYSQL_DATA *fields;

    /* XXX: need to sync with proxy? */
    printf("Sending query %s to backend\n", query);
    mysql_send_query(mysql_backend, query, length);

    /* derived from sql/client.c:cli_read_query_result */
    /* read info and result header packets */
    for (i=0; i<2; i++) {
        if ((pkt_len = backend_read_to_proxy(proxy)) == packet_error)
            return TRUE;
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

    if (mysql_backend->net.read_pos[0] == 254)
        printf("EOF\n");

    /* Flush the write buffer */
    net_flush(&proxy->net);

    return error;
}

/* Close the open connection to the backend */
void proxy_backend_close() {
    mysql_close(mysql_backend);
}
