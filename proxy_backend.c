#include "proxy.h"

#include <sql_common.h>
#include <client_settings.h>

#define MAX_PACKET_LENGTH (256L*256L*256L-1)
#define BACKENDS 10

static MYSQL *mysql_backend[BACKENDS];

static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *backend, MYSQL *proxy);

static pthread_mutex_t avail_mutex[BACKENDS];
static pthread_mutex_t availc_mutex;
static pthread_cond_t  availc_cv;

/* derived from sql/client.c:cli_safe_read */
static ulong backend_read_to_proxy(MYSQL *backend, MYSQL *proxy) {
    NET *net = &(backend->net);
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
static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields) {
    uchar *cp;
    uint field;
    ulong pkt_len = 8, len;

    /* Read until EOF (254) marker reached */
    while (*(cp = backend->net.read_pos) != 254 || pkt_len >= 8) {
        for (field=0; field<fields; field++) {
            len = (ulong) net_field_length(&cp);

            /* skip over size of field */
            if (len != NULL_LENGTH)
                cp += len;
        }

        /* Read and forward the row to the proxy */
        if ((pkt_len = backend_read_to_proxy(backend, proxy)) == packet_error)
            return TRUE;
    }

    /* success */
    return FALSE;
}

int proxy_backend_connect(char *host, int port, char *user, char *pass, char *db) {
    int i;

    /* Set default parameters
     * use empty strings to specify NULL */
    if (*user == '\0')
        user = NULL;
    if (*pass == '\0')
        pass = NULL;
    if (*db == '\0')
        db = NULL;

    for (i=0; i<BACKENDS; i++) {
        mysql_backend[i] = NULL;
        mysql_backend[i] = mysql_init(NULL);
        if (mysql_backend[i] == NULL) {
            proxy_error("Out of memory when allocating MySQL backend");
            return -1;
        }

        if (!mysql_real_connect(mysql_backend[i],
                    host, user, pass, db, port, NULL, 0)) {
            proxy_error("Failed to connect to MySQL backend: %s",
                    mysql_error(mysql_backend[i]));
            return -1;
        }

        /* Initialize the mutex for locking backend availability */
        pthread_mutex_init(&(avail_mutex[i]), NULL);
    }

    /* Initialize locks for signaling availability */
    pthread_mutex_init(&availc_mutex, NULL);
    pthread_cond_init(&availc_cv, NULL);

    return 0;
}

/* Try to get a lock on any backend */
int backend_try_locks() {
    int i;

    /* Check if any backend is available */
    for (i=0; i<BACKENDS; i++) {
        printf("Trying to lock backend %d\n", i);
        if (pthread_mutex_trylock(&(avail_mutex[i])) == 0) {
            printf("Got lock on %d!\n", i);
            return i;
        }
    }

    return -1;
}

/* Return the index of a backend not currently in use.
 * If all backends are in use, then wait until one
 * becomes available. */
int backend_choose() {
    int bi;

    /* See if any backends are available */
    if ((bi = backend_try_locks()) >= 0)
        return bi;

    printf("No joy. Waiting for backend to free\n");

    while (1) {
        /* Wait until a backend becomes free */
        pthread_mutex_lock(&availc_mutex);
        pthread_cond_wait(&availc_cv, &availc_mutex);

        printf("Backend free!\n");

        /* Now we're (almost) guaranteed backend access */
        bi = backend_try_locks();

        /* Unlock and return */
        pthread_mutex_unlock(&availc_mutex);

        /* We actually got a backend, return it
         * (otherwise, we go again) */
        if (bi >= 0)
            return bi;

        printf("Nevermind, no it isn't :(\n");
    }
}

my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    my_bool error = FALSE;
    ulong pkt_len = 8;
    uchar *pos;
    int i, bi = backend_choose();
    MYSQL *backend = mysql_backend[bi];

    printf("I've got a shiny new backend (%d)!\n", bi);

    /* XXX: need to sync with proxy? */
    printf("Sending query %s to backend\n", query);
    mysql_send_query(backend, query, length);

    /* derived from sql/client.c:cli_read_query_result */
    /* read info and result header packets */
    for (i=0; i<2; i++) {
        if ((pkt_len = backend_read_to_proxy(backend, proxy)) == packet_error) {
            error = TRUE;
            goto out;
        }

        /* If the query doesn't return results, no more to do */
        pos = (uchar*) backend->net.read_pos;
        if (net_field_length(&pos) == 0 || backend->net.read_pos[0] == 255) {
            error = FALSE;
            goto out; 
        }
    }

    /* read field info */
    if (backend_read_rows(backend, proxy, 7)) {
        error = TRUE;
        goto out;
    }

    /* Read result rows
     *
     * Here we assume the client has called mysql_store_result()
     * and wishes to retrieve all rows immediately. Otherwise,
     * the backend would be tied up waiting for the client to
     * decide when to fetch rows. (Clients using mysql_use_result()
     * should still function, but with possible network overhead.
     * */
    if (backend_read_rows(backend, proxy, backend->field_count)) {
        error = TRUE;
        goto out;
    }

out:
    /* Make the backend available again */
    printf("Backend %d...I don't know how to say this...I'm leaving you\n", bi);
    pthread_mutex_unlock(&(avail_mutex[bi]));
    return error;
}

/* Close the open connections to the backend
 * and destroy mutexes */
void proxy_backend_close() {
    int i;

    for (i=0; i<BACKENDS; i++) {
        mysql_close(mysql_backend[i]);
        pthread_mutex_destroy(&(avail_mutex[i]));
    }

    pthread_mutex_destroy(&availc_mutex);
    pthread_cond_destroy(&availc_cv);
}
