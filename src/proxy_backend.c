/******************************************************************************
 * proxy_backend.c
 *
 * Connect with backend servers and forward requests and replies.
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
#include <time.h>
#include <unistd.h>
#include <ltdl.h>

/** Maximum TCP packet length (from sql/net_serv.cc) */
#define MAX_PACKET_LENGTH (256L*256L*256L-1)

/** Array of backends currently available */
static proxy_host_t **backends = NULL;
/** Backend MySQL connections */
static proxy_backend_conn_t ***backend_conns;
/** Lock pool for controlling backend access */
static pool_t **backend_pools = NULL;
/** Total number of backends */
static int backend_num;
/** Query mapper for selecting backends */
static proxy_map_query_t backend_mapper = NULL;
/** ltdl handle to the mapper library */
static lt_dlhandle backend_mapper_handle = NULL;
/** Thread data structures for backend query threads */
static proxy_thread_t **backend_threads = NULL;
/** Pool for locking access to backend threads */
static pool_t **backend_thread_pool = NULL;

/** Signify that a backend is currently querying */
volatile sig_atomic_t querying = 0;

static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields, status_t *status);
static my_bool backend_proxy_write(MYSQL* __restrict backend, MYSQL* __restrict proxy, ulong pkt_len, status_t *status);
static ulong backend_read_to_proxy(MYSQL* __restrict backend, MYSQL* __restrict proxy, status_t *status);

static inline my_bool backend_query_idx(int bi, int ci, MYSQL *proxy, const char *query, ulong length, status_t *status);
static my_bool backend_query(proxy_backend_conn_t *conn, MYSQL *proxy, const char *query, ulong length, int bi, commitdata_t *commit, status_t *status);

/* Data structure allocation functions */
static void conn_free(proxy_backend_conn_t *conn);
static void backend_free(proxy_host_t *backend);
static void backends_free(proxy_host_t **backends, int num);
static void backend_conns_free(int bi);
static my_bool backends_alloc(int num_backends);
static void backend_conns_free(int bi);

static my_bool backend_connect(proxy_host_t *backend, proxy_backend_conn_t *conn);
static void backend_new_threads(int bi);
static proxy_host_t** backend_read_file(char *filename, int *num) __attribute__((malloc));


/**
 * Write from a backend to a proxy connection.
 *
 * \param backend Backend MYSQL object to read from.
 * \param proxy   Proxy MYSQL object to write to.
 * \param pkt_len Number of bytes to write.
 * \param status  Status of connection for updating bytes sent.
 **/
static my_bool backend_proxy_write(MYSQL* __restrict backend, MYSQL* __restrict proxy, ulong pkt_len, status_t *status) {
        NET *net = &backend->net;

        if (!proxy)
            return FALSE;

        /* Read from the backend and forward to the proxy connection */
        if (my_net_write(&(proxy->net), net->read_pos, (size_t) pkt_len)) {
            status->bytes_sent += pkt_len;
            proxy_log(LOG_ERROR, "Couldn't forward backend packet to proxy");
            return TRUE;
        }

        return FALSE;
}

/**
 * Read a MySQL packet from the backend and forward to the client.
 *
 * This code is derived from sql/client.c:cli_safe_read
 *
 * \param backend Backend to read from.
 * \param proxy   Client to write to or NULL to only read.
 * \param status  Status information for the connection.
 *
 * \return Length of the packet which was read.
 **/
static ulong backend_read_to_proxy(MYSQL* __restrict backend, MYSQL* __restrict proxy, status_t *status) {
    NET *net = &backend->net;
    ulong pkt_len;

    if (unlikely(!net->vio))
        return (packet_error);

    pkt_len = my_net_read(net);

    /* XXX: need to return error to client */
    if (unlikely(pkt_len == packet_error || pkt_len == 0)) {
        if (! (net->vio && vio_was_interrupted(net->vio))) {
            /* XXX: fatal error, close down 
             * should give soft error to client and reconnect with backend */
            proxy_log(LOG_ERROR, "Interrupted when reading backend response");
        } else
            proxy_log(LOG_ERROR, "Received error from backend");

        return packet_error;
    }

    /* XXX: could probably generate soft error
     * for client in below case */
    if (net->read_pos[0] == 255 && pkt_len <= 3)
        return packet_error;

    /* Write the results to the proxy */
    backend_proxy_write(backend, proxy, pkt_len, status);

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
 * \param status    Status information for the connection.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_read_rows(MYSQL *backend, MYSQL *proxy, uint fields, status_t *status) {
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
        if ((pkt_len = backend_read_to_proxy(backend, proxy, status)) == packet_error)
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
    char buf[BUFSIZ], *err;

    /* Load the query mapper */
    if (options.mapper != NULL) {
        /* Initialize ltdl */
        if (lt_dlinit())
            return TRUE;

        /* Construct the path to the mapper library */
        lt_dladdsearchdir("map/" LT_OBJDIR);
        lt_dladdsearchdir(PKG_LIB_DIR);

        snprintf(buf, BUFSIZ, "libproxymap-%s", options.mapper);

        if (!(backend_mapper_handle = lt_dlopenext(buf))) {
            err = (char*) lt_dlerror();
            proxy_log(LOG_ERROR, "Couldn't get handle to mapper %s:%s", options.mapper, err);
            return TRUE;
        }

        /* Grab the mapper from the library */
        backend_mapper = (proxy_map_query_t) (intptr_t) lt_dlsym(backend_mapper_handle, "proxy_map_query");

        /* Check for errors */
        err = (char*) lt_dlerror();

        if (err) {
            proxy_log(LOG_ERROR, "Couldn't load mapper %s:%s", options.mapper, err);
            return TRUE;
        }
    }

    /* Seed the RNG for later use */
    srand(time(NULL));

    return FALSE;
}

/**
 * Allocated data structures for storing backend info.
 *
 * \param num_backends Number of backends to allocate.
 * \param threading    TRUE to perform threading setup, FALSE otherwise.
 **/
static my_bool backends_alloc(int num_backends)  {
    int i, j;

    backend_num = num_backends;
    
    /* Allocate memory for backends and connections */
    if (num_backends > 0) {
        if (!backends) {
            backends = (proxy_host_t**) calloc(backend_num, sizeof(proxy_host_t*));
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

    /* Check if we skip threading and pool setup */
    if (!options.backend_file)
        return FALSE;

    /* Initialize pools for locking backend access */
    backend_pools = (pool_t**) calloc(backend_num, sizeof(proxy_host_t**));
    if (!backend_pools)
        goto error;

    for (i=0; i<num_backends; i++)
        backend_pools[i] = proxy_pool_new(backend_num);

    /* Create a thread pool */
    if (!backend_thread_pool) {
        backend_thread_pool = (pool_t**) calloc(backend_num, sizeof(pool_t*));
        for (i=0; i<backend_num; i++)
            backend_thread_pool[i] = proxy_pool_new(options.backend_threads);
    }

    /* Create backend threads */
    if (!backend_threads) {
        /* Start backend threads */
        backend_threads = calloc(backend_num, sizeof(proxy_thread_t));

        for (i=0; i<backend_num; i++)
            backend_new_threads(i);
    }

    return FALSE;

error:
    /* We failed to start, abort */
    proxy_backend_close();
    return TRUE;
}

/**
 * Start new threads for a particular backend.
 *
 * \param bi Index of the backend whose threads should be started.
 **/
static void backend_new_threads(int bi) {
    int i;
    proxy_thread_t *thread;
    pthread_attr_t attr;

    /* Set up thread attributes */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* Allocate space for the new threads */
    backend_threads[bi] = calloc(options.backend_threads, sizeof(proxy_thread_t));

    for (i=0; i<options.backend_threads; i++) {
        thread = &backend_threads[bi][i];
        thread->id = i;
        proxy_cond_init(&thread->cv);
        proxy_mutex_init(&thread->lock);
        thread->data.backend.bi = bi;

        thread->data.backend.conn = (proxy_backend_conn_t*) malloc(sizeof(proxy_backend_conn_t));
        thread->data.backend.conn->freed = FALSE;
        thread->data.backend.conn->mysql = NULL;

        thread->data.backend.query.query = NULL;

        /* Start a backend thread */
        proxy_threading_start(&thread->thread, &attr, proxy_backend_new_thread, (void*) thread);
    }
}

/**
 * Connect to a backend server with the given address.
 *
 * \param backend Address information of the backend.
 * \param conn    Connection whre MySQL object should be stored.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_connect(proxy_host_t *backend, proxy_backend_conn_t *conn) {
    MYSQL *mysql, *ret;
    my_bool reconnect = TRUE;

    mysql = conn->mysql = NULL;
    mysql = mysql_init(NULL);

    if (mysql == NULL) {
        proxy_log(LOG_ERROR, "Out of memory when allocating MySQL backend");
        return TRUE;
    }

    /* Reconnect if a backend connection is lost */
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);

    /* Connect to the backend */
    if (options.unix_socket) {
        proxy_log(LOG_INFO, "Connecting to %s", options.socket_file);
        ret = mysql_real_connect(mysql, NULL, options.user, options.pass, options.db,
                0, options.socket_file, 0);
    } else {
        proxy_log(LOG_INFO, "Connecting to %s:%d", backend->host, backend->port);
        ret = mysql_real_connect(mysql, backend->host,
                options.user, options.pass, options.db, backend->port, NULL, 0);
    }

    if (!ret) {
        proxy_log(LOG_ERROR, "Failed to connect to MySQL backend: %s",
                mysql_error(mysql));
        return TRUE;
    }

    /* Set autocommit option if specified */
    mysql_autocommit(mysql, options.autocommit && !options.two_pc);

    conn->mysql = mysql;

    return FALSE;
}

/**
 *  Read a list of backends from file.
 *
 *  \param filename Filename to read from.
 *  \param num      A pointer where the number of found backends will be stored.
 *
 *  \return An array of ::proxy_host_t structs representing the read backends.
 **/
static proxy_host_t** backend_read_file(char *filename, int *num) {
    FILE *f;
    char *buf, *buf2, *pch;
    ulong pos;
    uint i, c=0;
    proxy_host_t **new_backends;

    *num = -1;

    /* This case might happen when user sends SIGUSR1
     * without previously specifying a backend file */
    if (!filename) {
        proxy_log(LOG_ERROR, "No filename specified when reading backends");
        return NULL;
    }

    /* Open the file */
    f = fopen(filename, "r");
    if (!f) {
        proxy_log(LOG_ERROR, "Couldn't open backend file %s:%s", filename, errstr);
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
            proxy_log(LOG_ERROR, "Error reading from backend file %s:%s", filename, errstr);
        else if (feof(f))
            proxy_log(LOG_ERROR, "End of file when reading backends from %s", filename);
        else
            proxy_log(LOG_ERROR, "Unknown error reading backend file %s", filename);

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
    if (*num == 0) {
        proxy_log(LOG_ERROR, "Invalid number of backends %d\n", *num);
        free(buf);
        return NULL;
    }

    /* Allocate and read backends */
    new_backends = (proxy_host_t**) calloc(*num, sizeof(proxy_host_t*));
    i = 0;
    pch = strtok(buf, " \r\n\t");
    while (pch != NULL && (int)i<*num) {
        new_backends[i] = (proxy_host_t*) malloc(sizeof(proxy_host_t));

        /* If we have a colon, then a port number must have been specified */
        if ((buf2 = strchr(pch, ':'))) {
            new_backends[i]->host = strndup(pch, buf2-pch);
            new_backends[i]->port = atoi(buf2 + 1);
        } else {
            new_backends[i]->host = strdup(pch);
            new_backends[i]->port = 3306;
        }

        pch = strtok(NULL, " \r\n\t");
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

    backends[0] = (proxy_host_t*) malloc(sizeof(proxy_host_t));
    backends[0]->host = strdup(options.backend.host);
    backends[0]->port = options.backend.port;

    /* Connect to all backends */
    for (i=0; i<options.num_conns; i++) {
        if (backend_connect(backends[0], backend_conns[0][i]))
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

    /* Read the backends from the file */
    backends = backend_read_file(options.backend_file, &num_backends);
    if (!backends)
        return TRUE;

    /* Allocate backend data structures */
    if (backends_alloc(num_backends))
        return TRUE;

    /* Connect to all backends */
    for (i=0; i<num_backends; i++) {
        for (j=0; j<options.num_conns; j++) {
            if (backend_connect(backends[i], backend_conns[i][j]))
                return TRUE;
        }

        for (j=0; j<options.backend_threads; j++) {
            if (backend_connect(backends[i], backend_threads[i][j].data.backend.conn))
                return TRUE;
        }
    }

     return FALSE;
}

/**
 * Update data structures with new backend data after update.
 *
 * \param new_num      New number of backends.
 * \param new_backends New backends to use.
 **/
static inline void backends_switch(int new_num, proxy_host_t **new_backends) {
    int oldnum;
    proxy_host_t **old_backends;

    /* Switch to the new set of backends */
    old_backends = backends;
    backends = new_backends;

    oldnum = backend_num;
    backends_free(old_backends, oldnum);
    backend_num = new_num;
}

/**
 * Connect to new backends after an update.
 *
 * \param conns List of connections to open.
 * \param pools List of pools to initialize.
 **/
static inline void backends_new_connect(proxy_backend_conn_t ***conns, pool_t **pools) {
    int bi, ci;

    for (bi=0; bi<backend_num; bi++) {
        if (!conns[bi]) {
            proxy_log(LOG_INFO, "Connecting to new backend %d\n", bi);
            conns[bi] = (proxy_backend_conn_t**) calloc(options.num_conns, sizeof(proxy_backend_conn_t*));

            for (ci=0; ci<options.num_conns; ci++) {
                conns[bi][ci] = (proxy_backend_conn_t*) malloc(sizeof(proxy_backend_conn_t));
                backend_connect(backends[bi], conns[bi][ci]);
            }
        }

        /* Allocate a new pool if necessary */
        if (!pools[bi]) {
            pools[bi] = proxy_pool_new(options.num_conns);
        } else {
            /* Unlock pool */
            proxy_pool_unlock(pools[bi]);
        }

        /* Start new backend threads */
        if (!backend_threads[bi])
            backend_new_threads(bi);

        /* Create a new thread pool for this backend */
        if (!backend_thread_pool[bi]) {
            backend_thread_pool[bi] = proxy_pool_new(options.backend_threads);

            /* Open the MySQL connections for each backend thread */
            for (ci=0; ci<options.backend_threads; ci++) {
                if (!backend_threads[bi][ci].data.backend.conn->mysql)
                    backend_connect(backends[bi], backend_threads[bi][ci].data.backend.conn);
            }
        } else {
            proxy_pool_unlock(backend_thread_pool[bi]);
        }
    }
}

/**
 * Free a backend and asociated connections.
 *
 * \param bi Index of the backend to free.
 **/
static void backend_conns_free(int bi) {
    int i;

    /* Free connections */
    for (i=0; i<options.num_conns; i++) {
        if (proxy_pool_is_free(backend_pools[bi], i))
            conn_free(backend_conns[bi][i]);
        else
            backend_conns[bi][i]->freed = TRUE;
    }

    free(backend_conns[bi]);
    backend_conns[bi] = NULL;
    backend_free(backends[bi]);

    /* Destroy the pool */
    proxy_pool_destroy(backend_pools[bi]);
    backend_pools[bi] = NULL;

    /* Stop backend threads */
    for (i=0; i<options.backend_threads; i++)
        backend_threads[bi][i].data.backend.conn->freed = TRUE;

    proxy_threading_cancel(backend_threads[bi], options.backend_threads, backend_thread_pool[bi]);
    proxy_threading_cleanup(backend_threads[bi], options.backend_threads, backend_thread_pool[bi]);
}

/**
 * Reize the backend pool and connection data structures on update.
 *
 * \param num    Number of backends to allocate in new structure.
 * \param before TRUE if allocation is before reshuffling, FALSE otherwise.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_resize(int num, my_bool before) {
    int i;
    void *ptr;

    /* Reallocate memory */
    if ((before && num > backend_num) || (!before && num < backend_num)) {
        /* XXX: A little ugly below, but it ensures the proxy
         *      can continue without new backends if realloc fails */

#define SAFE_REALLOC(mem, size) \
        ptr = realloc(mem, num * size); \
        if (!ptr) { \
            return TRUE; \
            proxy_log(LOG_ERROR, "Could not allocate new memory for backends"); \
        } else { \
            mem = ptr; \
        }

        SAFE_REALLOC(backend_pools, sizeof(pool_t*));
        SAFE_REALLOC(backend_conns, sizeof(proxy_backend_conn_t**));
        SAFE_REALLOC(backend_threads, sizeof(proxy_thread_t*));
        SAFE_REALLOC(backend_thread_pool, sizeof(pool_t*));

#undef SAFE_REALLOC
    }

    /* Set new elements to NULL */
    if (!before && num > backend_num) {
        for (i=backend_num; i<num; i++) {
            backend_pools[i] = NULL;
            backend_conns[i] = NULL;
            backend_threads[i] = NULL;
            backend_thread_pool[i] = NULL;
        }
    }

    return FALSE;
}

/**
 * Update the list of backends from the previously loaded file.
 **/
void proxy_backends_update() {
    int num, i, j, keep[backend_num];
    my_bool changed = FALSE;
    proxy_host_t **new_backends = backend_read_file(options.backend_file, &num);

    /* Block others from getting backends */
    for (i=0; i<backend_num; i++) {
        proxy_pool_lock(backend_pools[i]);
        proxy_pool_lock(backend_thread_pool[i]);
    }

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
        if (backend_resize(num, TRUE)) {
            backends_free(new_backends, num);
            return;
        }
    } else {
        backends_free(new_backends, num);
        proxy_log(LOG_INFO, "No backends changed. Done.");
        return;
    }

    /* Clean up old backends */
    for (i=0; i<backend_num; i++) {
        if (keep[i] < 0) {
            proxy_log(LOG_INFO, "Disconnecting backend %d", i);
            backend_conns_free(i);
        } else {
            /* Save existing data */
            backend_pools[i] = backend_pools[keep[i]];
            backend_conns[i] = backend_conns[keep[i]];
            backend_thread_pool[i] = backend_thread_pool[keep[i]];
            backend_threads[i] = backend_threads[keep[i]];
        }
    }

    /* Finish resizing */
    backend_resize(num, FALSE);

    /* Switch to the new set of backends */
    backends_switch(num, new_backends);
    backends_new_connect(backend_conns, backend_pools);
}

/**
 * Start a new backend thread.
 *
 * \param ptr Pointer to a thread object.
 *
 * \return NULL.
 **/
void* proxy_backend_new_thread(void *ptr) {
    proxy_thread_t *thread = (proxy_thread_t*) ptr;
    proxy_backend_query_t *query = &thread->data.backend.query;

    proxy_threading_mask();
    proxy_mutex_lock(&thread->lock);

    while (1) {
        if(thread->exit) {
            proxy_mutex_unlock(&thread->lock);
            break;
        }

        /* Wait for work to be available */
        proxy_cond_wait(&thread->cv, &thread->lock);

        /* If no query specified, must be ready to exit */
        if (query->query == NULL) {
            proxy_mutex_unlock(&thread->lock);
            break;
        }

        __sync_fetch_and_add(&querying, 1);

        /* Send the query to the backend server */
        backend_query(thread->data.backend.conn, query->proxy,
                      query->query, *(query->length),
                      thread->data.backend.bi, thread->commit, thread->status);

        __sync_fetch_and_sub(&querying, 1);

        /* Signify thread availability */
        query->query = NULL;
        proxy_pool_return(backend_thread_pool[thread->data.backend.bi], thread->id);
    }

    proxy_log(LOG_INFO, "Exiting loop on backend %d, thead %d", thread->data.backend.bi, thread->id);
    pthread_exit(NULL);
}

/**
 * Send a query to the backend and return the results to the client.
 *
 * \param proxy  MySQL object corresponding to the client connection.
 * \param ci     Index of a connection to use in the case of a
 *               single backend.
 * \param query  A query string received from the client.
 * \param length Length of the query string.
 * \param commit Data required for synchronization
 *               and two-phase commit.
 * \param status Status information for the connection.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_backend_query(MYSQL *proxy, int ci, char *query, ulong length, commitdata_t *commit, status_t *status) {
    int bi = -1, i, ti;
    proxy_query_map_t map = QUERY_MAP_ANY;
    my_bool error = FALSE;
    char *newq = NULL;
    pthread_barrier_t query_barrier;
    proxy_backend_query_t *bquery;
    proxy_thread_t *thread;
    ulonglong results;

    /* Get the query map and modified query
     * if a mapper was specified */
    if (backend_mapper) {
        map = (*backend_mapper)(query, newq);

        /* If the query was modified by the mapper,
         * switch to the new query string */
        if (newq) {
            free(query);
            query = newq;
        }
        proxy_debug("Query %s mapped to %d", query, (int) map);
    }

    /* Spin until query can proceed */
    while (backend_pools && !backend_pools[0]) { usleep(1000); } /* XXX: should maybe lock here */
    while (cloning) { usleep(1000); }           /* Wait until cloning is done */

    /* Speed things up with only one backend
     * by avoiding synchronization */
    if (backend_num == 1)
        map = QUERY_MAP_ANY;

    switch (map) {
        case QUERY_MAP_ANY:
            /* Pick a random backend and get a connection.
             * We check for an unallocated pool in case
             * backends are in the process of changing */
            bi = rand() % backend_num;

            /* XXX: This guards against the unlikely case that we
             *      get here while backends are being updated */
            while (backend_pools && !backend_pools[bi]) { bi = rand() % backend_num; }

            if (backend_query_idx(bi, ci, proxy, query, length, status)) {
                error = TRUE;
                goto out;
            }
            break;

        case QUERY_MAP_ALL:
            /* Send a query to the other backends and keep only the first result */

            /* Add an identifier to the query if necessary */
            if (options.add_ids) {
                __sync_fetch_and_add(&transaction_id, 1);
                length += sprintf(query + length, "-- %lu", transaction_id);
            }

            /* Set up synchronization */
            pthread_barrier_init(&query_barrier, NULL, backend_num + 1);

            bi = rand() % backend_num;
            for (i=0; i<backend_num; i++) {
                /* Get the next backend */
                bi = (bi + 1) % backend_num;

                /* Dispatch threads for backend queries */
                ti = proxy_pool_get(backend_thread_pool[bi]);
                thread = &(backend_threads[bi][ti]);
                thread->status = status;

                proxy_mutex_lock(&(thread->lock));

                bquery = &(thread->data.backend.query);
                bquery->query = query;
                bquery->length = &length;
                bquery->proxy = (i == 0) ? proxy : NULL;

                /* Set up commit data */
                commit->backends = backend_num;
                commit->results = &results;
                commit->barrier = &query_barrier;
                thread->commit = commit;

                proxy_cond_signal(&thread->cv);
                proxy_mutex_unlock(&thread->lock);
            }

            /* Wait until all queries are complete */
            pthread_barrier_wait(&query_barrier);

            /* Free synchronization primitives */
            pthread_barrier_destroy(&query_barrier);

            /* XXX: should do better at handling failures */
            for (i=0; i<backend_num; i++)
                if (!(results & (i==0 ? 1 : 2 << (i-1)))) {
                    error = TRUE;
                    /* XXX should print a message if failure is not a malformed query */
                    //proxy_log(LOG_ERROR, "Failure for query on backend %d\n", i);
                }

            break;

        /* Some unknown value was returned, give up */
        default:
            error = TRUE;
            goto out;
    }

out:
    /* XXX: error reporting should be more verbose */
    return FALSE;
}

/**
 * Forward a query to a specific backend
 *
 * \param bi     Index of the backend to send the query to.
 * \param ci     Index of a connection to use, or negative
 *               to use any connection.
 * \param proxy  MYSQL object to forward results to.
 * \param query  Query string to execute.
 * \param length Length of the query.
 * \param status Status information for the connection.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static inline my_bool backend_query_idx(int bi, int ci, MYSQL *proxy, const char *query, ulong length, status_t *status) {
    proxy_backend_conn_t *conn;
    my_bool error;

    /* Get a backend to use */
    ci = backend_pools ? proxy_pool_get(backend_pools[bi]) : ci;
    conn = backend_conns[bi][ci];

    proxy_debug("Sending read-only query %s to backend %d, connection %d\n", query, bi, ci);

    /*Send the query */
    error = backend_query(conn, proxy, query, length, bi, NULL, status);

    if (!conn->freed && backend_pools)
        proxy_pool_return(backend_pools[bi], ci);

    return error;
}

/**
 * Wait for all backends to finish before continuing and record success.
 *
 * \param commit  Data required for synchronization.
 * \param status  Status information for the connection.
 * \param success Whether the query succeeded or failed.
 **/
static inline void backend_query_wait(commitdata_t *commit, int bi, my_bool success) {
    /* If we're sending to multiple backends, wait
     * until everyone is done before sending results */
    if (commit) {
        /* Record error status */
        if (commit->results && success) {
            __sync_fetch_and_or(commit->results, bi == 0 ? 1 : 2 << (bi-1));
        }
        if (commit->barrier) {
            pthread_barrier_wait(commit->barrier);
            commit->barrier = NULL;
        }
    }
}

/**
 * Forward a query to a backend connection
 *
 * \param conn    Connection where the query should be sent.
 * \param proxy   MYSQL object to forward results to.
 * \param query   Query string to execute.
 * \param length  Length of the query.
 * \param bi      Index of the backend executing the query.
 * \param commit  Data required for synchronization
 *                and two-phase commit.
 * \param status  Status information for the connection.
 *
 * \return TRUE on error, FALSE otherwise.
 **/
static my_bool backend_query(proxy_backend_conn_t *conn, MYSQL *proxy, const char *query, ulong length, int bi, commitdata_t *commit, status_t *status) {
    my_bool error = FALSE, success;
    ulong pkt_len = 8;
    MYSQL *mysql;

    /* Check for a valid MySQL object */
    mysql = conn->mysql;
    if (unlikely(!mysql)) {
        proxy_log(LOG_ERROR, "Query with uninitialized MySQL object");
        error = TRUE;
        goto out;
    }

    /* Send the query to the backend */
    proxy_debug("Sending query %s", query);

    /* Add an ID if necessary */
    if (!options.add_ids)
        mysql_send_query(mysql, query, length);
    else
        simple_command(mysql, COM_PROXY_QUERY, (uchar*) query, length, 1);

    /* Read the result header packet from the backend */
    pkt_len = backend_read_to_proxy(mysql, NULL, status);

    /* Error reading from the backend, wait on the barrier
     * so everyone else will be able to continue, then
     * return with error */
    if (pkt_len == packet_error) {
        error = TRUE;

        if (commit && commit->barrier)
            pthread_barrier_wait(commit->barrier);

        goto out;
    }

    /* Check the success of the transaction */
    success = (mysql->net.read_pos[0] != 0xFF) ? TRUE : FALSE;

    /* Wait for other backends to finish */
    backend_query_wait(commit, bi, success);

    /* Check if all transactions succeeded and commit or rollback accordingly */
    /* XXX: This currently assumes that queries requiring two-phase commit
     *      do not return any results, and thus have a single packet which
     *      has already been consumed at this point. This holds for UPDATE
     *      INSERT, and DELETE. If this assumption breaks, subsequent
     *      queries will fail, although the client can then reconnect. */
    if (commit && options.two_pc) {
        if (*(commit->results) == (ulonglong) ((2 << (commit->backends-1))-1)) {
            if (proxy)
                error = proxy_net_send_ok(proxy, 0, 0, 0);

            proxy_debug("Commiting on backend %d", bi);
            mysql_real_query(mysql, "COMMIT", 6);
        } else {
            if (proxy)
                error = proxy_net_send_error(proxy,
                        ER_ERROR_DURING_COMMIT,
                        "Couldn't commit transaction");
            proxy_debug("Rolling back on backend %d", bi);
            mysql_real_query(mysql, "ROLLBACK", 8);
        }

        return error;
    }

    /* Flush the write buffer */
    error = backend_proxy_write(mysql, proxy, pkt_len, status);
    proxy_net_flush(proxy);

    /* If query has zero results, then we can stop here */
    if (!success || net_field_length(&mysql->net.read_pos) == 0)
        return error;

    /* read field info */
    if (backend_read_rows(mysql, proxy, 7, status)) {
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
    if (backend_read_rows(mysql, proxy, mysql->field_count, status)) {
        error = TRUE;
        goto out;
    }

out:
    /* Free connection resources if necessary */
    if (conn->freed)
        conn_free(conn);

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
static void backend_free(proxy_host_t *backend) {
    if (!backend)
        return;

    free(backend->host);
    free(backend);
}

/**
 * Free an array of backends.
 *
 * \param backends Array of backends to free.
 * \param num      Number of backends in the array.
 **/
static void backends_free(proxy_host_t **backends, int num) {
    int i;

    if (!backends)
        return;

    /* Free all backends in the array */
    for (i=0; i<num; i++)
        backend_free(backends[i]);

    free(backends);
    backends = NULL;
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
        backend_conns[i] = NULL;

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
    backends_free(backends, backend_num);
    free(backend_conns);

    /* Free threads */
    if (backend_threads) {
        for (i=0; i<backend_num; i++) {
            /* Close connections */
            for (j=0; j<options.backend_threads; j++)
                conn_free(backend_threads[i][j].data.backend.conn);

            /* Shut down threads */
            proxy_threading_cancel(backend_threads[i], options.backend_threads, backend_thread_pool[i]);
            proxy_threading_cleanup(backend_threads[i], options.backend_threads, backend_thread_pool[i]);
        }

        free(backend_threads);
        free(backend_thread_pool);
    }
}
