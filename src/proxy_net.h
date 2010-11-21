/*
 * proxy_net.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_net_h
#define _proxy_net_h

#include <mysqld_error.h>

/** Name of proxy commands */
#define PROXY_CMD "PROXY"

#if __WORDSIZE == 64
#define LONG_LEN 20
#else
#define LONG_LEN 10
#endif

/* Define the size of the ID string added to queries
 * which is 3 for the comment specifier plus the
 * length of the maximum length of a long converted
 * to a string */
#define ID_SIZE 3 + LONG_LEN

/** Current transaction identifier */
extern ulong transaction_id;
/** Host which coordinates transactions between clones */
volatile MYSQL *coordinator;
/** Master hosts which handles cloning */
MYSQL *master;

/** Command to send for queries which must be tracked */
/* XXX: This will break if COM_END is equal to the size
 *      of an enum, but this will never happen */
#define COM_PROXY_QUERY COM_END+1

/**
 * Type of error on connection.
 **/
typedef enum {
    /** No error, keep going */
    ERROR_OK,
    /** No error, connection closed successfully. */
    ERROR_CLOSE,
    /** Error from client. */
    ERROR_CLIENT,
    /** Error from backend. */
    ERROR_BACKEND,
    /** Miscellaneous error. */
    ERROR_OTHER
} conn_error_t;

/** Total number of connections. */
long global_connections;
/** Globally accumulated status. */
status_t global_status;
/** Start time of the proxy server. */
time_t proxy_start_time;

my_bool proxy_net_handshake(MYSQL *mysql, struct sockaddr_in *clientaddr, int thread_id);
void* proxy_net_new_thread(void *ptr);
conn_error_t proxy_net_read_query(MYSQL *mysql, int thread_id, commitdata_t *commit, status_t *status);
my_bool proxy_net_send_ok(MYSQL *mysql, uint warnings, ulong affected_rows, ulonglong last_insert_id);
my_bool proxy_net_send_error(MYSQL *mysql, int sql_errno, const char *err);
void proxy_net_send_eof(MYSQL *mysql, status_t *status);

/**
 * Flush the write buffer of the proxy MySQL object
 *
 * @param proxy MySQL object for proxy to flush.
 *
 * /return TRUE on error, FALSE otherwise.
 **/
static inline my_bool proxy_net_flush(MYSQL *proxy) {
    if ((intptr_t) proxy)
        return net_flush(&(proxy->net));
    else
        return FALSE;
}

#endif /* _proxy_net_h */
