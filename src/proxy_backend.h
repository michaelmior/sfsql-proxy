/*
 * proxy_backend.h
 *
 * Connect with backend servers and forward requests and replies.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_backend_h
#define _proxy_backend_h

/**
 * Connection indices used during the
 * lifetime of a client connection.
 **/
typedef struct {
    int bi;
    int ci;
} proxy_conn_idx_t;

/**
 * Connection information for backends.
 **/
typedef struct {
    /** Hostname or IP of the backend to connect to. */
    char *host;
    /** Port number of the associated host. */
    int port;
} proxy_host_t;

/**
 * Backend connection information.
 **/
typedef struct {
    /** MySQL object associated with connection. */
    MYSQL *mysql;

    /** If the connection should be freed when the
        current user is finished. */
    my_bool freed; 
                   
} proxy_backend_conn_t;

/**
 * Query structure required for backend threads.
 **/
typedef struct {
    /** Query to execute. */
    char *query;
    /** Length of the query string. */
    ulong *length;
    /** Proxy MySQL object where results
        should be sent, or NULL to discard. */
    MYSQL *proxy;             
} proxy_backend_query_t;

/** Data required to process a backend query. */
typedef struct {
    /** Index of the backend being used. */
    int bi;
    /** Backend connection. */
    proxy_backend_conn_t *conn;
    /** Query information. */
    proxy_backend_query_t query;
} proxy_backend_data_t;

int proxy_backend_num();
my_bool proxy_backend_init();
my_bool proxy_backend_connect();
my_bool proxy_backends_connect();
my_bool proxy_backend_query(MYSQL *proxy, proxy_conn_idx_t *conn_idx, char *query, ulong length, my_bool replicated, commitdata_t *commit, status_t *status);
void* proxy_backend_new_thread(void *ptr);
my_bool proxy_backend_clone_complete(int *clone_ids, int nclones, ulong clone_trans_id, my_bool commit);
my_bool proxy_backend_add(char *host, int port);
void proxy_backend_get_connection(proxy_conn_idx_t *conn_idx, int thread_id);
void proxy_backend_release_connection(proxy_conn_idx_t *conn_idx);
void proxy_backend_close();

extern volatile sig_atomic_t querying;
extern volatile sig_atomic_t committing;

#endif /* _proxy_backend_h */
