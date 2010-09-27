/*
 * proxy_backend.h
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
 * Connection information for backends.
 **/
typedef struct {
    /** Hostname or IP of the backend to connect to. */
    char *host;
    /** Port number of the associated host. */
    int port;
} proxy_backend_t;

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
    /** Condition variable for syncing access to count. */
    pthread_cond_t *cv;
    /** Mutex associated with condition variable. */
    pthread_mutex_t *mutex;
    /** Barrier for ensuring all queries execute
        before sending results. */
    pthread_barrier_t *barrier;
    /** Index of the backend the query should be sent to. */
    int bi;
    /** Success array from various backends. */
    my_bool *result;
    /** Count of backends which have executed the query. */
    int *count;
} proxy_backend_query_t;

my_bool proxy_backend_init();
my_bool proxy_backend_connect();
my_bool proxy_backends_connect();
void proxy_backends_update();
my_bool proxy_backend_query(MYSQL *proxy, char *query, ulong length);
void* proxy_backend_new_thread(void *ptr);
void proxy_backend_close();

extern volatile sig_atomic_t querying;

#endif /* _proxy_backend_h */