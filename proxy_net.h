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

#include <my_base.h>

void proxy_net_handshake(MYSQL *mysql, struct sockaddr_in *clientaddr, int thread_id);
void* proxy_net_new_thread(void *ptr);
int proxy_net_read_query(MYSQL *mysql);
my_bool proxy_net_send_ok(MYSQL *mysql, uint warnings, ha_rows affected_rows, ulonglong last_insert_id);

/**
 * Flush the write buffer of the proxy MySQL object
 *
 * /param proxy MySQL object for proxy to flush.
 *
 * /return TRUE on error, FALSE otherwise.
 **/
static inline my_bool proxy_net_flush(MYSQL *proxy) {
    if (likely((intptr_t) proxy))
        return net_flush(&(proxy->net));
    else
        return FALSE;
}

#endif /* _proxy_net_h */
