/******************************************************************************
 * proxy_backend.c
 *
 * Connect with backend servers and forward requests and replies
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

#include "proxy.h"

#include <sql_common.h>
#include <client_settings.h>

#define MAX_PACKET_LENGTH (256L*256L*256L-1)

static proxy_backend_t **backends;
static pool_t *backend_pool;
static my_bool backend_autocommit;
static int backend_num;
static char *backend_user;
static char *backend_pass;
static char *backend_db;

static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *backend, MYSQL *proxy);
void backend_init(char *user, char *pass, char *db, int num_backends, my_bool autocommit);
static int backend_connect(proxy_backend_t *backend, int num);
proxy_backend_t* backend_read_file(char *filename, int *num);

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

/* Set up backend data structures */
void backend_init(char *user, char *pass, char *db, int num_backends, my_bool autocommit) {
    /* Set default parameters use empty strings
     * to specify NULL */
    if (*user == '\0')
        user = NULL;
    if (*pass == '\0')
        pass = NULL;
    if (*db == '\0')
        db = NULL;

    /* Save connection info */
    backend_user = user;
    backend_pass = pass;
    backend_db =   db;
    backend_num =  num_backends;
    backend_autocommit = autocommit;

    /* Initialize a pool for locking backend access */
    backend_pool = proxy_pool_new(backend_num);
    
    backends = (proxy_backend_t**) calloc(backend_num, sizeof(proxy_backend_t*));
}

/* Connect to a backend server with the given address and autocommit option */
static int backend_connect(proxy_backend_t *backend, int num) {
    MYSQL *mysql;

    mysql = backend->mysql = NULL;
    mysql = mysql_init(NULL);

    if (mysql == NULL) {
        proxy_error("Out of memory when allocating MySQL backend");
        return -1;
    }

    if (!mysql_real_connect(mysql,
                backend->host, backend_user, backend_pass, backend_db, backend->port, NULL, 0)) {
        proxy_error("Failed to connect to MySQL backend: %s",
                mysql_error(mysql));
        return -1;
    }

    /* Set autocommit option if specified */
    mysql_autocommit(mysql, backend_autocommit);

    backend->mysql = mysql;
    backends[num] = backend;

    return 0;
}

/* Read a list of backends from file */
proxy_backend_t* backend_read_file(char *filename, int *num) {
    FILE *f = fopen(filename, "r");
    char *buf, *pch;
    long pos;
    int i, c=0;
    proxy_backend_t *new_backends;

    /* Get the end of the file */
    fseek(f, 0, SEEK_END);
    pos = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Read the entire file */
    buf = (char*) malloc(pos);
    fread(buf, pos, 1, f);
    fclose(f);

    /* Count number of non-empty lines */
    *num = 0;
    for (i=0; i<pos; i++) {
        if (buf[i] == '\n') {
            if (c) {
                (*num)++;
                c = 0;
            }
        } else if(buf[i] != '\r' && buf[i] != '\t' && buf[i] != ' ') {
            c++;
        }
    }

    /* Allocate and read backends */
    new_backends = (proxy_backend_t*) calloc(*num, sizeof(proxy_backend_t));
    i = 0;
    pch = strtok(buf, " :\r\n\t");
    while (pch != NULL) {
        new_backends[i].host = strdup(pch);
        pch = strtok(NULL, " :\r\n\t");
        new_backends[i].port = atoi(pch);

        new_backends[i].mysql = NULL;

        pch = strtok(NULL, " :\r\n\t");
        i++;
    }

    free(buf);
    return new_backends;
}

int proxy_backend_connect(proxy_backend_t *backend, char *user, char *pass, char *db, int num_backends, my_bool autocommit) {
    int i;

    backend_init(user, pass, db, num_backends, autocommit);

    /* Connect to all backends */
    for (i=0; i<backend_num; i++)
        if (backend_connect(backend, i) < 0)
            return -1;

    backend_autocommit = autocommit;

    return 0;
}

int proxy_backends_connect(char *file, char *user, char *pass, char *db, my_bool autocommit) {
    int num_backends, i;
    proxy_backend_t *backends = NULL;

    backends = backend_read_file(file, &num_backends);
    backend_init(user, pass, db, num_backends, autocommit);

    for (i=0; i<num_backends; i++)
        if (backend_connect(&(backends[i]), i))
            return -1;
 
     return 0;
 }

my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    my_bool error = FALSE;
    ulong pkt_len = 8;
    uchar *pos;

    /* The call below will block until a backend is free  */
    int i, bi = proxy_get_from_pool(backend_pool);
    proxy_backend_t *backend = backends[bi];
    MYSQL *mysql = backend->mysql;

    /* XXX: need to sync with proxy? */
    printf("Sending query %s to backend %d\n", query, bi);
    mysql_send_query(mysql, query, length);

    /* derived from sql/client.c:cli_read_query_result */
    /* read info and result header packets */
    for (i=0; i<2; i++) {
        if ((pkt_len = backend_read_to_proxy(mysql, proxy)) == packet_error) {
            error = TRUE;
            goto out;
        }

        /* If the query doesn't return results, no more to do */
        pos = (uchar*) mysql->net.read_pos;
        if (net_field_length(&pos) == 0 || mysql->net.read_pos[0] == 255) {
            error = FALSE;
            goto out; 
        }
    }

    /* read field info */
    if (backend_read_rows(mysql, proxy, 7)) {
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
    if (backend_read_rows(mysql, proxy, mysql->field_count)) {
        error = TRUE;
        goto out;
    }

out:
    /* Make the backend available again */
    proxy_return_to_pool(backend_pool, bi);
    return error;
}

/* Close the open connections to the backend
 * and destroy mutexes */
void proxy_backend_close() {
    int i;

    /* Destroy lock pool */
    proxy_pool_destroy(backend_pool);

    /* Close connection with backends */
    for (i=0; i<backend_num; i++)
        mysql_close(backends[i]->mysql);

    free(backends);
}
