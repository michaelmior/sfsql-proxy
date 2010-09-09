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
#include "map/proxy_map.h"

#include <sql_common.h>
#include <client_settings.h>
#include <time.h>
#include <unistd.h>
#include <ltdl.h>

static char BUF[BUFSIZ];

#define MAX_PACKET_LENGTH (256L*256L*256L-1)     /** Maximum TCP packet length (from sql/net_serv.cc) */
#define MAX_BACKENDS      128                    /** Maximum number of backends. Must be a power of 2 for LCG. */

static proxy_backend_t **backends = NULL;        /** Array of backends currently available */
static proxy_backend_conn_t ***backend_conns;    /** Backend MySQL connections */
static pool_t **backend_pools = NULL;            /** Lock pool for controlling backend access */
static int backend_num;                          /** Total number of backends */
static proxy_map_query_t backend_mapper = NULL;  /** Query mapper for selecting backends */
static lt_dlhandle backend_mapper_handle = NULL; /** ltdl handle to the mapper library */

static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields);
static ulong backend_read_to_proxy(MYSQL *backend, MYSQL *proxy);
static my_bool backend_connect(proxy_backend_t *backend, proxy_backend_conn_t *conn);
static proxy_backend_t** backend_read_file(char *filename, int *num);
static void conn_free(proxy_backend_conn_t *conn);
static void backend_free(proxy_backend_t *backend);
static my_bool backends_alloc(int num_backends);
static my_bool backend_query_idx(int bi, MYSQL *proxy, const char *query, ulong length);

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

    if (proxy) {
        /* Read from the backend and forward to the proxy connection */
        if (my_net_write(&(proxy->net), net->read_pos, (size_t) pkt_len)) {
            proxy_error("Couldn't forward backend packet to proxy");
            return (packet_error);
        }
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
            proxy_net_flush(proxy);
        }
    }

    /* Final flush */
    proxy_net_flush(proxy);

    /* success */
    return FALSE;
}

/**
 *  Set up backend data structures.
 *
 *  \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backend_init() {
    char *buf = NULL;

    /* Load the query mapper */
    if (options.mapper != NULL) {
        /* Initialize ltdl */
        if (lt_dlinit())
            return TRUE;

        /* Construct the path to the mapper library */
        buf = (char*) malloc(BUFSIZ);
        snprintf(buf, BUFSIZ, "map/" LT_OBJDIR "libproxymap-%s", options.mapper);

        if (!(backend_mapper_handle = lt_dlopenext(buf))) {
            free(buf);
            return TRUE;
        }

        /* Grab the mapper from the library */
        backend_mapper = (proxy_map_query_t) (intptr_t) lt_dlsym(backend_mapper_handle, "proxy_map_query");

        /* Check for errors */
        free(buf);
        buf = (char*) lt_dlerror();

        if (buf) {
            proxy_error("Couldn't load mapper %s:%s", options.mapper, buf);
            free(buf);
        }
    }

    /* Seed the RNG for later use */
    srand(time(NULL));

    return FALSE;
}

/**
 * Allocated data structures for storing backend info
 *
 * \param num_backends Number of backends to allocate.
 **/
static my_bool backends_alloc(int num_backends)  {
    int i, j;

    backend_num = num_backends;
    
    /* Allocate memory for backends and connections */
    if (num_backends > 0) {
        if (!backends) {
            backends = (proxy_backend_t**) calloc(backend_num, sizeof(proxy_backend_t*));
            if (!backends)
                goto error;
        }

        if (!backend_conns) {
            backend_conns = (proxy_backend_conn_t***) calloc(backend_num, sizeof(proxy_backend_conn_t**));
            if (!backend_conns)
                goto error;

            for (i=0; i<backend_num; i++) {
                backend_conns[i] = (proxy_backend_conn_t**) calloc(options.num_conns, sizeof(proxy_backend_conn_t*));
                if (!backend_conns[i])
                    goto error;

                for (j=0; j<options.num_conns; j++) {
                    backend_conns[i][j] = (proxy_backend_conn_t*) malloc(sizeof(proxy_backend_conn_t));
                    if (!backend_conns[i][j])
                        goto error;

                    backend_conns[i][j]->mysql = NULL;
                    backend_conns[i][j]->freed = FALSE;
                }
            }
        }
    }

    /* Initialize pools for locking backend access */
    backend_pools = (pool_t**) calloc(backend_num, sizeof(proxy_backend_t**));
    if (!backend_pools)
        goto error;

    for (i=0; i<num_backends; i++)
        backend_pools[i] = proxy_pool_new(backend_num);

    return FALSE;

error:
    proxy_backend_close();
    return TRUE;
}

/**
 * Connect to a backend server with the given address.
 *
 * \param backend Address information of the backend.
 * \param conn    Connection whre MySQL object should be stored.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_connect(proxy_backend_t *backend, proxy_backend_conn_t *conn) {
    MYSQL *mysql;

    mysql = conn->mysql = NULL;
    mysql = mysql_init(NULL);

    if (mysql == NULL) {
        proxy_error("Out of memory when allocating MySQL backend");
        return TRUE;
    }

    if (!mysql_real_connect(mysql,
                backend->host, options.user, options.pass, options.db, backend->port, NULL, 0)) {
        proxy_error("Failed to connect to MySQL backend: %s",
                mysql_error(mysql));
        return TRUE;
    }

    /* Set autocommit option if specified */
    mysql_autocommit(mysql, options.autocommit);

    conn->mysql = mysql;

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
proxy_backend_t** backend_read_file(char *filename, int *num) {
    FILE *f;
    char *buf, *pch;
    ulong pos;
    uint i, c=0;
    proxy_backend_t **new_backends;

    *num = -1;

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
    buf = (char*) malloc(pos+1);
    if (fread(buf, 1, pos, f) != pos) {
        if (ferror(f))
            proxy_error("Error reading from backend file %s:%s", filename, errstr);
        else if (feof(f))
            proxy_error("End of file when reading backends from %s", filename);
        else
            proxy_error("Unknown error reading backend file %s", filename);

        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[pos] = '\0';

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

    /* Make sure number of backends is valid */
    if (*num == 0 || *num > MAX_BACKENDS) {
        proxy_error("Invalid number of backends %d\n", *num);
        free(buf);
        return NULL;
    }

    /* Allocate and read backends */
    new_backends = (proxy_backend_t**) calloc(*num, sizeof(proxy_backend_t*));
    i = 0;
    pch = strtok(buf, " :\r\n\t");
    while (pch != NULL && (int)i<*num) {
        new_backends[i] = (proxy_backend_t*) malloc(sizeof(proxy_backend_t));

        new_backends[i]->host = strdup(pch);
        pch = strtok(NULL, " :\r\n\t");
        new_backends[i]->port = atoi(pch);

        pch = strtok(NULL, " :\r\n\t");
        i++;
    }

    free(buf);
    return new_backends;
}

/**
 * Open a number of connections to a single backend.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backend_connect() {
    int i;

    if (backends_alloc(1))
        return TRUE;

    backends[0] = (proxy_backend_t*) malloc(sizeof(proxy_backend_t));
    backends[0]->host = strdup(options.backend.host);
    backends[0]->port = options.backend.port;

    /* Connect to all backends */
    for (i=0; i<options.num_conns; i++) {
        if (backend_connect(backends[0], backend_conns[0][i]) < 0)
            return TRUE;
    }

    return FALSE;
}

/**
 * Connect to all backends in a specified file.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backends_connect() {
    int num_backends=-1, i, j;

    backends = backend_read_file(options.backend_file, &num_backends);
    if (!backends)
        return TRUE;

    if (backends_alloc(num_backends))
        return TRUE;

    for (i=0; i<num_backends; i++) {
        printf("Connecting to %s:%d\n", backends[i]->host, backends[i]->port);

        for (j=0; j<options.num_conns; j++) {
            if (backend_connect(backends[i], backend_conns[i][j]))
                return TRUE;
        }
    }
 
     return FALSE;
}

/**
 * Update the list of backends from the previously loaded file.
 **/
void proxy_backends_update() {
    int num, oldnum, i, j, keep[backend_num];
    my_bool changed = FALSE;
    proxy_backend_t **new_backends = backend_read_file(options.backend_file, &num), **old_backends;
    pool_t **new_pools = NULL, **old_pools;
    proxy_backend_conn_t ***new_conns = NULL, ***old_conns;

    /* Block others from getting backends */
    for (i=0; i<backend_num; i++)
        proxy_pool_lock(backend_pools[i]);

    /* Compare the current backends with the new ones to
     * see if there are any which can be reused */
    for (i=0; i<backend_num; i++) {
        keep[i] = -1;

        for (j=0; j<num; j++) {
            /* Check for backends already connected */
            if (strcmp(backends[i]->host, new_backends[j]->host) == 0
                    && backends[i]->port == new_backends[j]->port) {
                keep[i] = j;
                break;
            }
        }

        if (keep[i] < 0)
            changed = TRUE;
    }

    /* Reallocate data structures if necessary */
    if (backend_num != num || changed) {
        new_pools = (pool_t**) calloc(num, sizeof(pool_t*));
        new_conns = (proxy_backend_conn_t***) calloc(num, sizeof(proxy_backend_conn_t**));

        for (j=0; j<num; j++) {
            new_pools[j] = NULL;
            new_conns[j] = NULL;
        }
    }

    /* Clean up old backends */
    for (i=0; i<backend_num; i++) {
        if (keep[i] < 0) {
            /* Free connections */
            for (j=0; j<options.num_conns; j++) {
                if (proxy_pool_is_free(backend_pools[i], j)) {
                    proxy_pool_remove(backend_pools[i], j);
                    conn_free(backend_conns[i][j]);
                } else {
                    backend_conns[i][j]->freed = TRUE;
                }
            }

            free(backend_conns[i]);
            backend_free(backends[i]);
            
            /* Destroy the pool */
            proxy_pool_destroy(backend_pools[i]);
            backend_pools[i] = NULL;
        } else {
            /* Save existing data */
            new_pools[keep[i]] = backend_pools[i];
            new_conns[keep[i]] = backend_conns[i];
        }
    }

    /* Switch to the new set of backends */
    old_backends = backends;
    backends = new_backends;
    free(old_backends);
    oldnum = backend_num;
    backend_num = num;

    /* Nothing new here */
    if (!changed && oldnum == num) {
        printf("No backends changed. Done.\n");
        return;
    }

    /* Switch to new resources */
    old_pools = backend_pools;
    backend_pools = new_pools;
    free(old_pools);

    old_conns = backend_conns;
    backend_conns = new_conns;
    free(old_conns);

    /* Connect to new backends */
    for (i=0; i<backend_num; i++) {
        if (!backend_conns[i]) {
            backend_conns[i] = (proxy_backend_conn_t**) calloc(options.num_conns, sizeof(proxy_backend_conn_t*));

            for (j=0; j<options.num_conns; j++) {
                backend_conns[i][j] = (proxy_backend_conn_t*) malloc(sizeof(proxy_backend_conn_t));
                backend_connect(backends[i], backend_conns[i][j]);
            }
        }

        /* Allocate a new pool if necessary */
        if (!backend_pools[i]) {
            backend_pools[i] = proxy_pool_new(options.num_conns);
        } else {
            /* Unlock pool */
            proxy_pool_unlock(backend_pools[i]);
        }
    }
}

/**
 * Linear congruential generator for picking backends in random order. 
 *
 * \param X Previous value returned by ::lcg, -1 for first value.
 * \param N Maximum value to generate (must be less than ::MAX_BACKENDS).
 *
 * \return The next value in the random sequence.
 **/
int lcg(int X, int N) {
    static int m = MAX_BACKENDS;
    static int c = 17;
    static int s;
    int a;

    /* Give an invalid result for inavlid input */
    if (N > MAX_BACKENDS)
        return -1;

    /* Pick a new random starting value */
    if (X < 0)
        s = rand();

    a = ((s * 4) + 1) & (m - 1);

    do { X = (a * X + c) & (m - 1); } while (X >= N);

    return X;
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
my_bool proxy_backend_query(MYSQL *proxy, char *query, ulong length) {
    int bi, i;
    proxy_query_map_t *map = NULL;
    enum QUERY_MAP type = QUERY_MAP_ANY;
    my_bool error = FALSE;
    //char *oq, *oq2;

    /* Get the query map and modified query
     * if a mapper was specified */
    if (backend_mapper) {
        map = (*backend_mapper)(query);

        type = map->map;
        if (map->query)
            query = map->query;
        printf("Query %s mapped to %d\n", query, (int) type);
    }

    switch (type) {
        case QUERY_MAP_ANY:
            /* Pick a random backend and get a connection.
             * We check for an unallocated pool in case
             * backends are in the process of changing */
            bi = rand() % backend_num;
            while (!backend_pools[bi]) { bi = rand() % backend_num; }
            if (backend_query_idx(bi, proxy, query, length)) {
                error = TRUE;
                goto out;
            }
            break;
        case QUERY_MAP_ALL:
            /* Send a query to the other backends and keep only the first result */
            /* XXX: For some reason, mysql_send_query messes with the value of
             *      query even though it's declared const. The strdup-ing should
             *      be unnecessary. */
            bi = -1;
            for (i=0; i<backend_num; i++) {
                /* Get the next backend from the LCG */
                bi = lcg(bi, backend_num);
                while (!backend_pools[bi]) { usleep(1000); } /* XXX: should maybe lock here */

                //oq = strdup(query);
                //oq2 = query;
                if (backend_query_idx(bi, i == 0 ? proxy: NULL, query, length)) {
                    error = TRUE;
                    goto out;
                }
                //free(oq2);
                //query = oq;
            }
            //free(oq);

            break;
        default:
            error = TRUE;
            goto out;
    }

out:
    /* Free resources associated with the map */
    if (map) {
        if (map->query)
            free(map->query);
        free(map);
    }
    return error;
}

/**
 * Forward a query to a specific backend
 *
 * \param bi     Index of the backend to send the query to.
 * \param proxy  MYSQL object to forward results to.
 * \param query  Query string to execute.
 * \param length Length of the query.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_query_idx(int bi, MYSQL *proxy, const char *query, ulong length) {
    my_bool error = FALSE;
    ulong pkt_len = 8;
    uchar *pos;
    int ci, i;
    MYSQL *mysql;
    proxy_backend_conn_t *conn;
    char *oq;

    ci = proxy_pool_get(backend_pools[bi]);
    conn = backend_conns[bi][ci];
    mysql = conn->mysql;

    /* MySQL reassigns the pointer, so we restore it after the call */
    oq = (char*) query;
    printf("Sending query %s to backend %d\n", query, bi);
    mysql_send_query(mysql, query, length);
    query = oq;

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
    proxy_net_flush(proxy);

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
    /* Free connection resources if this connection
     * has gone away, otherwise, return to pool */
    if (conn->freed)
        conn_free(conn);
    else
        proxy_pool_return(backend_pools[bi], ci);

    return error;
}

/**
 * Free resources associated with a connection.
 *
 * \param conn Connection to free.
 **/
static void conn_free(proxy_backend_conn_t *conn) {
    if (!conn)
        return;

    if (conn->mysql)
        mysql_close(conn->mysql);

    free(conn);
}

/**
 * Free resources associated with a backend.
 *
 * \param backend Backend to free.
 **/
static void backend_free(proxy_backend_t *backend) {
    if (!backend)
        return;

    free(backend->host);
    free(backend);
}

/**
 * Close the open connections to the backend
 * and destroy mutexes.
 **/
void proxy_backend_close() {
    int i, j;

    /* Close connections and destroy lock pools */
    for (i=0; i<backend_num; i++) {
        for (j=0; j<options.num_conns; j++)
            conn_free(backend_conns[i][j]);
        free(backend_conns[i]);

        backend_free(backends[i]);

        if (backend_pools)
            proxy_pool_destroy(backend_pools[i]);
    }

    /* Free ltdl resources associated with
     * the mapper library */
    if (backend_mapper_handle)
        lt_dlclose(backend_mapper_handle);
    if (backend_mapper)
        lt_dlexit();

    /* Free allocated memory */
    free(backend_pools);
    free(backends);
    free(backend_conns);
}
