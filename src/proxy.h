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

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "config.h"

#include <my_sys.h>
#include <m_string.h>
#include <m_ctype.h>
#include <mysql_com.h>
#include <violite.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <mysql.h>

#include <pthread.h>

/** Signify that we are currently cloning */
extern volatile sig_atomic_t cloning;

/** Data required for two-phase commit. */
typedef struct {
    /** Barrier for ensuring all queries execute
        before sending results. */
    pthread_barrier_t *barrier;
    /** Number of active backends when the
     *  transacation was issued. */
    int backends;
    /** Success array from various backends. */
    ulonglong *results;
} commitdata_t;

/**
 * Status information for a client
 * connection or global statistics.
 **/
typedef struct {
    /** Bytes received from clients by proxy. */
    long bytes_recv;
    /** Bytes sent by proxy to client. */
    long bytes_sent;
    /** Number of queries received by proxy. */
    long queries;
} status_t;

#include "proxy_logging.h"
#include "proxy_net.h"
#include "proxy_backend.h"
#include "proxy_pool.h"
#include "proxy_threading.h"
#include "proxy_clone.h"
#include "proxy_monitor.h"
#include "proxy_options.h"

/** Threads for dealing with connected clients. */
proxy_thread_t *net_threads;
/** Thread pool for managing connected clients. */
pool_t *thread_pool;

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

/**
 * Copied from client/sql_string.h since this
 * function is not included in the client library. */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
