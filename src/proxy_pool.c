/******************************************************************************
 * proxy_pool.c
 *
 * Maintain a pool of locks to control access to resources.
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

/**
 * Create a new lock pool with a specified size.
 *
 * @param size Size of the pool to create.
 *
 * @return Newly created pool.
 **/
pool_t* proxy_pool_new(int size) {
    int i, alloc=1;
    pool_t *new_pool;
    pthread_mutexattr_t attr;

    if (size <= 0)
        return NULL;

    srand(time(NULL));

    new_pool = (pool_t *) malloc(sizeof(pool_t));

    /* Allocate memory for the lock pool */
    new_pool->size = size;
    new_pool->locked = 0;

    /* Find the nearest power of two */
    while (alloc < size)
        alloc <<= 1;

    new_pool->avail = (my_bool*) calloc(alloc, sizeof(my_bool));

    /* Set up availability */
    for (i=0; i<alloc; i++)
        new_pool->avail[i] = TRUE;

    new_pool->__alloc = alloc;

    /* Initialize mutexes */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&new_pool->lock, &attr);

    proxy_cond_init(&new_pool->avail_cv);
    proxy_mutex_init(&new_pool->avail_mutex);

    return new_pool;
}

/**
 * Block others from accessing the pool.
 *
 * @param pool Pool to be blocked.
 **/
void proxy_pool_lock(pool_t *pool) {
    proxy_mutex_lock(&pool->lock);
}

/**
 * Unblock others from accessing the pool.
 *
 * @param pool Pool to be unblocked.
 **/
void proxy_pool_unlock(pool_t *pool) {
    proxy_mutex_unlock(&pool->lock);
}

/**
 * Modify the size of an existing pool, allocating memory as necessary.
 *
 * @param pool Pool to resize.
 * @param size New size of the pool.
 **/
void proxy_pool_set_size(pool_t *pool, int size) {
    int alloc=1, i;
    my_bool *avail;

    if (size == pool->size)
        return;

    pthread_mutex_lock(&pool->lock);

    /* Get the new allocated size */
    while (alloc < size)
        alloc <<= 1;

    /* Allocate a new array and copy old availability */
    if (alloc != pool->__alloc) {
        avail = (my_bool*) calloc(alloc, sizeof(my_bool));

        /* Check if we are shrinking */
        if (pool->size > alloc)
            pool->size = alloc;

        /* Copy old availability */
        for (i=0; i<pool->size; i++)
            avail[i] = pool->avail[i];

        /* Set new resources to be available */
        for (i=pool->size; i<alloc; i++)
            avail[i] = TRUE;

        free(pool->avail);
        pool->avail = avail;
        pool->__alloc = alloc;
    }

    pool->size = size;

    pthread_mutex_unlock(&pool->lock);
}

/**
 * Remove an item from a pool and resize accordingly.
 *
 * @param pool Pool that the item should be removed from.
 * @param idx  Index of the item to remove.
 **/
void proxy_pool_remove(pool_t *pool, int idx) {
    int i;

    if (idx >= pool->size)
        return;

    proxy_pool_set_size(pool, pool->size-1);

    /* Shift all elements */
    for (i=idx; i<pool->size-1; i++)
        pool->avail[i] = pool->avail[i+1];
}

/**
 * Try to find an available item in the pool.
 *
 * @param pool Pool to check.
 *
 * @return Index of an available item, or negative if no items are available.
 **/
static int pool_try_locks(pool_t *pool) {
    int i, pi;

    pthread_mutex_lock(&pool->lock);

    /* Check availability of items in the pool */
    pi = rand() % pool->size;
    for (i=0; i<pool->size; i++) {
        pi = (pi + 1) % pool->size;

        if (pool->avail[pi]) {
            pool->avail[pi] = FALSE;
            pool->locked++;
            pthread_mutex_unlock(&pool->lock);
            return pi;
        }
    }

    pthread_mutex_unlock(&pool->lock);
    return -1;
}

/**
 * Get an available item from a pool, waiting if necessary.
 *
 * @callergraph
 *
 * @param pool Pool to check.
 *
 * @return Index of an available item in the pool.
 **/
int proxy_pool_get(pool_t *pool) {
    int idx;

    if ((idx = pool_try_locks(pool)) >= 0)
        return idx;

    /* Wait for something to become available */
    while (1) {
        proxy_mutex_lock(&pool->avail_mutex);
        proxy_cond_wait(&pool->avail_cv, &pool->avail_mutex);

        idx = pool_try_locks(pool);

        proxy_mutex_unlock(&pool->avail_mutex);

        if (idx >= 0)
            return idx;
    }
}

/**
 * Check if an item in the pool is free.
 *
 * @param pool Pool to check.
 * @param idx  Index to check.
 *
 * @return TRUE if the item is free, FALSE otherwise.
 **/
my_bool proxy_pool_is_free(pool_t *pool, int idx) {
    my_bool ret;

    if (idx > pool->size)
        return FALSE;

    pthread_mutex_lock(&pool->lock);
    ret = pool->avail[idx];
    pthread_mutex_unlock(&pool->lock);

    return ret;
}

/**
 * Get the next item in a pool which is currently locked.
 *
 * @param pool Pool to check.
 *
 * @return Index of a locked item, or negative if no items are locked.
 **/
int proxy_pool_get_locked(pool_t *pool) {
    int i;

    pthread_mutex_lock(&pool->lock);

    for (i=0; i<pool->size; i++) {
        if (!(pool->avail[i])) {
            pthread_mutex_unlock(&pool->lock);
            return i;
        }
    }

    pthread_mutex_unlock(&pool->lock);
    return -1;
}

/**
 * Return a locked item to the pool.
 *
 * @callergraph
 *
 * @param pool Pool the item should be returned to.
 * @param idx  Index of the item to return
 **/
void proxy_pool_return(pool_t *pool, int idx) {
    /* Update the item availability */
    pthread_mutex_lock(&pool->lock);
    if (pool->avail[idx]) {
        proxy_log(LOG_ERROR, "Trying to free lock from already free pool");
    } else {
        pool->locked--;
        pool->avail[idx] = TRUE;
    }
    pthread_mutex_unlock(&pool->lock);

    /* Signify availability in case someone is waiting */
    proxy_mutex_lock(&pool->avail_mutex);
    proxy_cond_signal(&pool->avail_cv);
    proxy_mutex_unlock(&pool->avail_mutex);
}

/**
 * Free all memory and mutexes associated with the pool.
 *
 * @param pool Pool to destroy.
 **/
void proxy_pool_destroy(pool_t *pool) {
    if (!pool)
        return;

    /* Unlock the mutex if locked */
    pthread_mutex_trylock(&pool->lock);
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);

    free(pool->avail);

    proxy_cond_destroy(&pool->avail_cv);
    proxy_mutex_destroy(&pool->avail_mutex);

    /* Finally, free the pool itself */
    free(pool);
}
