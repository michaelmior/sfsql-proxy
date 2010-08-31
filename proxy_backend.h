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
    MYSQL *mysql;
    my_bool freed;
} proxy_backend_t;

my_bool proxy_backend_connect(proxy_backend_t *backend, char *user, char *pass, char *db, int num_backends, my_bool autocommit);
my_bool proxy_backends_connect(char *file, char *user, char *pass, char *db, my_bool autocommit);
void proxy_backends_update();
my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length);
void proxy_backend_close();

#endif /* _proxy_backend_h */
