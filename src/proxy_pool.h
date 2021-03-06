/*
 * proxy_pool.h
 *
 * Maintain a pool of locks to control access to resources.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_pool_h
#define _proxy_pool_h

/**
 * Data structure for lock pool implemenation
 * with list of availability of a set of items. */
typedef struct {
    /** Current size of the pool. */
    int size;
    /** Currently allocated size of the pool. */
    int __alloc;
    /** Number of items currently locked in the pool. */
    int locked;
    /** List of availabilities of items in the pool. */
    my_bool *avail;
    /** Lock to block pool access. */
    pthread_mutex_t lock;
    /** Condition variable for signifying new availability. */
    pthread_cond_t avail_cv;
    /** Lock assocated with condition variable. */
    pthread_mutex_t avail_mutex;
} pool_t;

pool_t* proxy_pool_new(int size)
    __attribute__((malloc));
void proxy_pool_lock(pool_t *pool);
void proxy_pool_unlock(pool_t *pool);
void proxy_pool_set_size(pool_t *pool, int size);
void proxy_pool_remove(pool_t *pool, int idx);
int proxy_pool_get(pool_t *pool);
void proxy_pool_return(pool_t *pool, int idx);
my_bool proxy_pool_is_free(pool_t *pool, int idx);
int proxy_pool_get_locked(pool_t *pool);
void proxy_pool_destroy(pool_t *pool);

#endif /* _proxy_pool_h */
