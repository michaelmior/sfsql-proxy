/******************************************************************************
 * proxy_pool.c
 *
 * Maintain a pool of locks to control access to resources
 *
 * Copyright (c) 2010, Michael Mior <mmior@cs.toronto.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include "proxy.h"

static int pool_try_locks(pool_t *pool);

pool_t* proxy_pool_new(int size) {
    int i;
    pool_t *new_pool = (pool_t *) malloc(sizeof(pool_t));

    /* Allocate memory for the lock pool */
    new_pool->size = size;
    new_pool->locks = (pthread_mutex_t*) calloc(size, sizeof(pthread_mutex_t));
    new_pool->avail_cv = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    new_pool->avail_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));

    /* Initialize mutexes */
    for (i=0; i<size; i++)
        proxy_mutex_init(&(new_pool->locks[i]));

    proxy_cond_init(new_pool->avail_cv);
    proxy_mutex_init(new_pool->avail_mutex);

    return new_pool;
}

static int pool_try_locks(pool_t *pool) {
    int i;

    /* Check if any object in the pool is already available */
    for (i=0; i<pool->size; i++) {
        if (proxy_mutex_trylock(&(pool->locks[i])) == 0)
            return i;
    }

    return -1;
}

int proxy_get_from_pool(pool_t *pool) {
    int idx;

    /* See if any objects are available */
    if ((idx = pool_try_locks(pool)) >= 0)
        return idx;

    /* Wait for something to become available */
    while (1) {
        printf("No more threads :( I'll wait...\n");
        proxy_mutex_lock(pool->avail_mutex);
        proxy_cond_wait(pool->avail_cv, pool->avail_mutex);

        idx = pool_try_locks(pool);

        proxy_mutex_unlock(pool->avail_mutex);

        if (idx >= 0)
            return idx;
    }
}

/* Get the next item in the pool which is currently locked */
int proxy_pool_get_locked(pool_t *pool) {
    int i;

    for (i=0; i<pool->size; i++) {
        if (proxy_mutex_trylock(&(pool->locks[i])) == EBUSY)
            return i;

        proxy_mutex_unlock(&(pool->locks[i]));
    }

    return -1;
}

void proxy_return_to_pool(pool_t *pool, int idx) {
    printf("You can have %d back\n", idx);
    
    /* Unlock the associated mutex if locked */
    if (proxy_mutex_trylock(&(pool->locks[idx])) == EBUSY)
        proxy_error("Trying to free lock from already free pool");
    proxy_mutex_unlock(&(pool->locks[idx]));

    /* Signify availability in case someone is waiting */
    proxy_mutex_lock(pool->avail_mutex);
    proxy_cond_signal(pool->avail_cv);
    proxy_mutex_unlock(pool->avail_mutex);
}

void proxy_pool_destroy(pool_t *pool) {
    int i;

    /* Free all the mutexes */
    for (i=0; i<pool->size; i++) {
        /* Unlock the mutex if locked */
        proxy_mutex_trylock(&(pool->locks[i]));
        proxy_mutex_unlock(&(pool->locks[i]));

        proxy_mutex_destroy(&(pool->locks[i]));
    }
    free(pool->locks);

    proxy_cond_destroy(pool->avail_cv);
    free(pool->avail_cv);
    proxy_mutex_destroy(pool->avail_mutex);
    free(pool->avail_mutex);

    /* Finally, free the pool itself */
    free(pool);
}
