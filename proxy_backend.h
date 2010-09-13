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
 * Connection information for backends */
typedef struct {
    char *host;
    int port;
} proxy_backend_t;

typedef struct {
    MYSQL *mysql;
    my_bool freed;
} proxy_backend_conn_t;

typedef struct {
    char *query;
    ulong *length;
    MYSQL *proxy;
    pthread_mutex_t *mutex;
    pthread_cond_t *cv;
    int bi;
    my_bool *result;
    int *count;
} proxy_backend_query_t;

my_bool proxy_backend_init();
my_bool proxy_backend_connect();
my_bool proxy_backends_connect();
void proxy_backends_update();
my_bool proxy_backend_query(MYSQL *proxy, char *query, ulong length);
void* proxy_backend_new_thread(void *ptr);
void proxy_backend_close();

#endif /* _proxy_backend_h */
