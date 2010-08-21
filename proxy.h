/* 
 * proxy.h
 *
 * Main proxy include file with useful macros, structs, and default configuration options
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_h
#define _proxy_h

#include <my_global.h>
#include <my_sys.h>
#include <m_string.h>
#include <m_ctype.h>
#include <mysql_com.h>
#include <violite.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <mysql.h>
#include <pthread.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)
void __proxy_error(const char*loc, const char *fmt, ...);
#define proxy_error(fmt, ...) __proxy_error(AT, fmt, ##__VA_ARGS__)

#include "proxy_net.h"
#include "proxy_backend.h"
#include "proxy_pool.h"
#include "proxy_threading.h"

#define BACKEND_HOST  "127.0.0.1"
#define BACKEND_PORT  3306
#define BACKEND_USER  "root"
#define BACKEND_PASS  "root"
#define BACKEND_DB    "test"

#define PROXY_PORT    4040
#define PROXY_THREADS 10

pool_t *thread_pool;

typedef struct st_proxy_work {
    int clientfd;
    struct sockaddr_in *addr;
    MYSQL *proxy;
} proxy_work_t;

typedef struct st_proxy_thread {
    int id;
    pthread_t thread;
    pthread_cond_t cv;
    pthread_mutex_t lock;
    proxy_work_t *work;
} proxy_thread_t;
proxy_thread_t threads[PROXY_THREADS];

/* from client/sql_string.h */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
