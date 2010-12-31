/*
 * proxy_clone.h
 *
 * Functionality related to SnowFlock cloning.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_clone_h
#define _proxy_clone_h

#include "hashtable/hashtable.h"

/** Identifier of the server (clone ID). */
extern volatile sig_atomic_t server_id;

/** Signify that we are currently cloning. */
extern volatile sig_atomic_t cloning;

/** Current generation of clones. */
extern volatile sig_atomic_t clone_generation;

void proxy_clone_init();
void proxy_clone_end();
my_bool proxy_clone_prepare(int nclones);
int proxy_do_clone(int nclones, char **err, int errlen);
void proxy_clone_complete();
my_bool proxy_clone_wait();
void proxy_clone_notify();

extern struct hashtable *clone_table;

DEFINE_HASHTABLE_INSERT(_proxy_clone_insert, proxy_host_t);
DEFINE_HASHTABLE_SEARCH(_proxy_clone_search, proxy_host_t);
DEFINE_HASHTABLE_REMOVE(_proxy_clone_remove, proxy_host_t);

/**
 * Insert a new transaction in the transaction hashtable.
 *
 * @param clone_id Clone ID to use as key.
 * @param ip       IP address of the clone.
 *
 * @return Zero on success, anything else on failure.
 **/
inline __attribute__((always_inline)) int proxy_clone_insert(ulong clone_id, proxy_host_t *host) {
    proxy_debug("Adding clone %lu to hashtable with address %s:%d", clone_id, host->host, host->port);
    return _proxy_clone_insert(clone_table, clone_id, host);
}

/**
 * Find a clone in the clone hashtable.
 *
 * @param clone_id Clone ID to use as key.
 *
 * @return The IP of the clone, or NULL if the clone
 *         could not be found.
 **/
inline __attribute__((always_inline)) proxy_host_t* proxy_clone_search(ulong clone_id) {
    return _proxy_clone_search(clone_table, clone_id);
}

/**
 * Remove a clone from the clone hashtable.
 *
 * @param clone_id Clone ID to use as key.
 *
 * @return The IP of the removed clone, or NULL if no clone
 *         exists with the specified ID.
 **/
inline __attribute__((always_inline)) proxy_host_t* proxy_clone_remove(ulong clone_id) {
    proxy_debug("Removing clone %lu from hashtable", clone_id);
    return _proxy_clone_remove(clone_table, clone_id);
}

#endif /* _proxy_clone_h */
