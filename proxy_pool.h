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

typedef struct st_pool {
    int size;
    pthread_mutex_t *locks;
    pthread_cond_t *avail_cv;
    pthread_mutex_t *avail_mutex;
} pool_t;

pool_t* proxy_pool_new(int size);
int proxy_get_from_pool(pool_t *pool);
void proxy_return_to_pool(pool_t *pool, int idx);
int proxy_pool_get_locked(pool_t *pool);
void proxy_pool_destroy(pool_t *pool);

#endif /* _proxy_pool_h */
