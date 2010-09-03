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
typedef struct st_proxy_backend {
    char *host;
    int port;
} proxy_backend_t;

typedef struct st_proxy_backend_conn_t {
    MYSQL *mysql;
    my_bool freed;
} proxy_backend_conn_t;

void proxy_backend_init(char *user, char *pass, char *db, int num_conns, my_bool autocommit);
my_bool proxy_backend_connect(proxy_backend_t *backend);
my_bool proxy_backends_connect(char *file);
void proxy_backends_update();
my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length);
void proxy_backend_close();

#endif /* _proxy_backend_h */
