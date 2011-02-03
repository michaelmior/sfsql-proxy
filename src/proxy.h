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
    /** Specifies when final results are committed
     *  and the proxy can be released */
    pthread_spinlock_t committed;
    /** Signifies that at least one backend
     *  has begun to commit this transaction. */
    volatile sig_atomic_t committing;
} commitdata_t;

/**
 * Status information for a client
 * connection or global statistics.
 **/
typedef struct {
    /** Bytes received from clients by proxy. */
    ulong bytes_recv;
    /** Bytes sent by proxy to client. */
    ulong bytes_sent;
    /** Number of queries received by proxy. */
    ulong queries;
    /** Number of non-replicated queries. */
    ulong queries_any;
    /** Number of replicated queries. */
    ulong queries_all;
} status_t;

/**
 * Reset a :status_t struct to it's default values.
 *
 * @param status Status to be reset.
 **/
static inline void proxy_status_reset(status_t *status) {
    status->bytes_sent = 0;
    status->bytes_recv = 0;
    status->queries = 0;
    status->queries_any = 0;
    status->queries_all = 0;
}

/**
 * Add the contents of one status variable to another.
 *
 * @param src         Status variable to add.
 * @param[in,out] dst Status variable to be added to.
 **/
static inline void proxy_status_add(status_t *src, status_t *dst) {
    (void) __sync_fetch_and_add(&dst->bytes_sent, src->bytes_sent);
    (void) __sync_fetch_and_add(&dst->bytes_recv, src->bytes_recv);
    (void) __sync_fetch_and_add(&dst->queries, src->queries);
    (void) __sync_fetch_and_add(&dst->queries_any, src->queries_any);
    (void) __sync_fetch_and_add(&dst->queries_all, src->queries_all);
}

#include "proxy_logging.h"
#include "proxy_backend.h"
#include "proxy_net.h"
#include "proxy_pool.h"
#include "proxy_threading.h"
#include "proxy_clone.h"
#include "proxy_monitor.h"
#include "proxy_cmd.h"
#include "proxy_trans.h"
#include "proxy_options.h"

/** Threads for dealing with connected clients. */
proxy_thread_t *net_threads;
/** Thread pool for managing connected clients. */
pool_t *thread_pool;

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

/** Time passed to usleep for synchronization */
#define SYNC_SLEEP 100

/**
 * Copied from client/sql_string.h since this
 * function is not included in the client library. */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
