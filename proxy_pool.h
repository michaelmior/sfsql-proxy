/* 
 * proxy_pool.h
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
    int size, __alloc;
    my_bool *avail;
    pthread_mutex_t lock;
    pthread_cond_t *avail_cv;
    pthread_mutex_t *avail_mutex;
} pool_t;

pool_t* proxy_pool_new(int size);
void proxy_pool_set_size(pool_t *pool, int size);
void proxy_pool_remove(pool_t *pool, int idx);
int proxy_get_from_pool(pool_t *pool);
void proxy_return_to_pool(pool_t *pool, int idx);
int proxy_pool_get_locked(pool_t *pool);
void proxy_pool_destroy(pool_t *pool);

#endif /* _proxy_pool_h */
