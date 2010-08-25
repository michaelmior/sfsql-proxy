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

#define BACKEND_HOST  "127.0.0.1" /** Default backend host */
#define BACKEND_PORT  3306        /** Default backend port */
#define BACKEND_USER  "root"      /** Default backend user */
#define BACKEND_PASS  "root"      /** Default backend password */
#define BACKEND_DB    "test"      /** Default backend database */
#define NUM_BACKENDS  10          /** Default number of backends */

#define PROXY_PORT    4040        /** Default port to listen on for incoming connections */
#define PROXY_THREADS 10          /** Default number of threads started to do client work */

pool_t *thread_pool;

/**
 * All information needed by threads
 * to connect to clients and begin working. */
typedef struct {
    int clientfd;
    struct sockaddr_in *addr;
    MYSQL *proxy;
} proxy_work_t;

/**
 * Data structures needed for thread pool
 * implementation and signaling of new work. */
typedef struct {
    int id;
    pthread_t thread;
    pthread_cond_t cv;
    pthread_mutex_t lock;
    proxy_work_t *work;
} proxy_thread_t;
proxy_thread_t threads[PROXY_THREADS];

/**
 * Copied from client/sql_string.h since this
 * function is not included in the client library. */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
