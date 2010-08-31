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

static char BUF[BUFSIZ];

#define MAX_PACKET_LENGTH (256L*256L*256L-1) /** Maximum TCP packet length (from sql/net_serv.cc) */

static proxy_backend_t **backends = NULL; /** Array of backends currently available */
static pool_t *backend_pool;              /** Lock pool for controlling backend access */
static my_bool backend_autocommit;        /** Global autocommit option */
static int backend_num;                   /** Total number of backends */
static char *backend_user;                /** Username for all backends */
static char *backend_pass;                /** Password for all backends */
static char *backend_db;                  /** Database for all backends */
static char *backend_file;                /** Filename where backends were read from */

static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *backend, MYSQL *proxy);
static void backend_init(char *user, char *pass, char *db, int num_backends, my_bool autocommit);
static my_bool backend_connect(proxy_backend_t *backend);
static proxy_backend_t** backend_read_file(char *filename, int *num);

/**
 * Read a MySQL packet from the backend and forward to the client.
 *
 * This code is derived from sql/client.c:cli_safe_read
 *
 * \param backend Backend to read from.
 * \param proxy   Client to write to.
 *
 * \return Length of the packet which was read.
 **/
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

    return pkt_len;
}

/**
 * After a query is sent to the backend, read resulting rows
 * and forward to the client connection.
 *
 * This code is derived from sql/client.c:cli_read_rows
 *
 * \param backend Backend where results are being read from.
 * \param proxy   Client where results are written to.
 * \param fields  Number of fields in the result set.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields) {
    uchar *cp;
    uint field;
    ulong pkt_len = 8, len, total_len=0;

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

        total_len += pkt_len;
        if (total_len >= MAX_PACKET_LENGTH) {
            total_len = 0;
            /* Flush the write buffer */
            net_flush(&(proxy->net));
        }
    }

    /* Final flush */
    net_flush(&(proxy->net));

    /* success */
    return FALSE;
}

/**
 *  Set up backend data structures.
 *
 *  \param user         Username for all backends.
 *  \param pass         Password for all backends.
 *  \param db           Database for all backends.
 *  \param num_backends Total number of backends.
 *  \param autocommit   Autocommit option to set on all backends.
 **/
static void backend_init(char *user, char *pass, char *db, int num_backends, my_bool autocommit) {
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
    
    if (!backends)
        backends = (proxy_backend_t**) calloc(backend_num, sizeof(proxy_backend_t*));
}

/**
 * Connect to a backend server with the given address.
 *
 * \param backend Address information of the backend.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_connect(proxy_backend_t *backend) {
    MYSQL *mysql;

    mysql = backend->mysql = NULL;
    mysql = mysql_init(NULL);

    if (mysql == NULL) {
        proxy_error("Out of memory when allocating MySQL backend");
        return TRUE;
    }

    if (!mysql_real_connect(mysql,
                backend->host, backend_user, backend_pass, backend_db, backend->port, NULL, 0)) {
        proxy_error("Failed to connect to MySQL backend: %s",
                mysql_error(mysql));
        return TRUE;
    }

    /* Set autocommit option if specified */
    mysql_autocommit(mysql, backend_autocommit);

    backend->mysql = mysql;

    return FALSE;
}

/**
 *  Read a list of backends from file.
 *
 *  \param filename Filename to read from.
 *  \param num      A pointer where the number of found backends will be stored.
 *
 *  \return An array of ::proxy_backend_t structs representing the read backends.
 **/
static proxy_backend_t** backend_read_file(char *filename, int *num) {
    FILE *f;
    char *buf, *pch;
    ulong pos;
    uint i, c=0;
    proxy_backend_t **new_backends;

    /* This case might happen when user sends SIGUSR1
     * without previously specifying a backend file */
    if (!filename) {
        proxy_error("No filename specified when reading backends");
        return NULL;
    }

    /* Open the file */
    f = fopen(filename, "r");
    if (!f) {
        proxy_error("Couldn't open backend file %s:%s", filename, errstr);
        return NULL;
    }

    /* Get the end of the file */
    fseek(f, 0, SEEK_END);
    pos = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Read the entire file */
    buf = (char*) malloc(pos);
    if (fread(buf, pos, 1, f) != pos) {
        fclose(f);
        if (ferror(f))
            proxy_error("Error reading from backend file %s:%s", filename, errstr);
        else if (feof(f))
            proxy_error("End of file when reading backends from %s", filename);
        else
            proxy_error("Unknown error reading backend file %s", filename);
        return NULL;
    }
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
    new_backends = (proxy_backend_t**) calloc(*num, sizeof(proxy_backend_t*));
    i = 0;
    pch = strtok(buf, " :\r\n\t");
    while (pch != NULL) {
        new_backends[i] = (proxy_backend_t*) malloc(sizeof(proxy_backend_t));

        new_backends[i]->host = strdup(pch);
        pch = strtok(NULL, " :\r\n\t");
        new_backends[i]->port = atoi(pch);

        new_backends[i]->mysql = NULL;

        pch = strtok(NULL, " :\r\n\t");
        i++;
    }

    free(buf);
    return new_backends;
}

/**
 * Open a number of connections to a single backend.
 *
 * \param backend      Backend connection information.
 * \param user         Username to use when connecting to the backend.
 * \param pass         Password to use when connecting to the backend.
 * \param db           Database to be selected after connecting.
 * \param num_backends Number of connections to open.
 * \param autocommit   Whether autocommit mode should be enabled on backends.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backend_connect(proxy_backend_t *backend, char *user, char *pass, char *db, int num_backends, my_bool autocommit) {
    int i;

    backend_init(user, pass, db, num_backends, autocommit);

    /* Connect to all backends */
    for (i=0; i<backend_num; i++) {
        /* Allocate the new backend */
        backends[i] = (proxy_backend_t*) malloc(sizeof(proxy_backend_t));
        backends[i]->host = strdup(backend->host);
        backends[i]->port = backend->port;
        backends[i]->mysql = NULL;

        if (backend_connect(backends[i]) < 0)
            return TRUE;
    }

    backend_autocommit = autocommit;

    return FALSE;
}

/**
 * Connect to all backends in a specified file.
 *
 * \param file       Filename of a file which contains a list of backends in the format host:port.
 * \param user       Username to use when connecting to the backend.
 * \param pass       Password to use when connecting to the backend.
 * \param db         Database to be selected after connecting.
 * \param autocommit Whether autocommit mode should be enabled on backends.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backends_connect(char *file, char *user, char *pass, char *db, my_bool autocommit) {
    int num_backends=-1, i;

    backend_file = file;

    backends = backend_read_file(file, &num_backends);
    backend_init(user, pass, db, num_backends, autocommit);

    if (!backends)
        return TRUE;

    for (i=0; i<num_backends; i++)
        if (backend_connect(backends[i]))
            return TRUE;
 
     return FALSE;
 }

/**
 * Send a query to the backend and return the results to the client.
 *
 * \param proxy  MySQL object corresponding to the client connection.
 * \param query  A query string received from the client.
 * \param length Length of the query string.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length) {
    my_bool error = FALSE;
    ulong pkt_len = 8;
    uchar *pos;

    /* The call below will block until a backend is free  */
    int i, bi = proxy_pool_get(backend_pool);
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

    /* Flush the write buffer */
    net_flush(&(proxy->net));

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
    proxy_pool_return(backend_pool, bi);
    return error;
}

/**
 * Close the open connections to the backend
 * and destroy mutexes.
 **/
void proxy_backend_close() {
    int i;

    /* Destroy lock pool */
    proxy_pool_destroy(backend_pool);

    if (!backends)
        return;

    /* Close connection with backends */
    for (i=0; i<backend_num; i++) {
        if (backends[i]->mysql) {
            mysql_close(backends[i]->mysql);
            free(backends[i]->host);
            free(backends[i]);
        }
    }

    free(backends);
}
