/*
 * proxy_trans.h
 *
 * Manage a hash table for transactions so we can do lookups
 * upon receiving messages for two-phase commit.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_trans_h
#define _proxy_trans_h

#include "hashtable/hashtable.h"

/** Hashtable to hold transactions */
extern struct hashtable *trans_table;

/**
 * Holds data required for the decision to
 * commit or roll back a transaction.
 **/
typedef struct {
    /** Number of clones which have agreed to commit. */
    volatile sig_atomic_t num;
    /* Total number which must agree to commit. */
    int total;

    /* TRUE to commit, FALSE to roll back */
    my_bool success;

    /** Condition variable for notifying threads of
     *  new commit information. */
    pthread_cond_t cv;
    /** Mutex for locking access to condition variable. */
    pthread_mutex_t cv_mutex;
} proxy_trans_t;

void proxy_trans_init();
void proxy_trans_end();

DEFINE_HASHTABLE_INSERT(_proxy_trans_insert, ulong, proxy_trans_t);
DEFINE_HASHTABLE_SEARCH(_proxy_trans_search, ulong, proxy_trans_t);
DEFINE_HASHTABLE_REMOVE(_proxy_trans_remove, ulong, proxy_trans_t);

/**
 * Insert a new transaction in the transaction hashtable.
 *
 * @param transaction_id Pointer to the transaction ID to use as key.
 * @param trans          Pointer to a transaction struct to store.
 *
 * @return Zero on success, anything else on failure.
 **/
inline __attribute__((always_inline)) int proxy_trans_insert(ulong *transaction_id, proxy_trans_t *trans) {
    proxy_debug("Adding transaction %lu to hashtable", *transaction_id);
    return _proxy_trans_insert(trans_table, transaction_id, trans);
}

/**
 * Find a transaction in the transaction hashtable.
 *
 * @param transaction_id Pointer to the transaction ID to use as key.
 *
 * @return The found transaction, or NULL if the transaction
 * could not be found.
 **/
inline __attribute__((always_inline)) proxy_trans_t* proxy_trans_search(ulong *transaction_id) {
    proxy_debug("Looking up transaction %lu in hashtable", *transaction_id);
    return _proxy_trans_search(trans_table, transaction_id);
}

/**
 * Remove a transaction from the transaction hashtable.
 *
 * @param transaction_id Pointer to the transaction ID to use as key.
 *
 * @return The removed transaction, or NULL if no transaction
 *         exists with the specified key.
 **/
inline __attribute__((always_inline)) proxy_trans_t* proxy_trans_remove(ulong *transaction_id) {
    proxy_debug("Removing transaction %lu from hashtable", *transaction_id);
    return _proxy_trans_remove(trans_table, transaction_id);
}

#endif /* _proxy_trans_h */
