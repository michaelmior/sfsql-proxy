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

/**
 * Create a new lock pool with a specified size.
 *
 * \param size Size of the pool to create.
 *
 * \return Newly created pool.
 **/
pool_t* proxy_pool_new(int size) {
    int i, alloc=1;
    pool_t *new_pool;

    if (size <= 0)
        return NULL;

    new_pool = (pool_t *) malloc(sizeof(pool_t));

    /* Allocate memory for the lock pool */
    new_pool->size = size;

    /* Find the nearest power of two */
    while (alloc < size)
        alloc <<= 1;

    new_pool->avail = (my_bool*) calloc(alloc, sizeof(my_bool));
    new_pool->avail_cv = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    new_pool->avail_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));

    /* Set up availability */
    for (i=0; i<alloc; i++)
        new_pool->avail[i] = TRUE;

    new_pool->__alloc = alloc;

    /* Initialize mutexes */
    proxy_mutex_init(&(new_pool->lock));
    proxy_cond_init(new_pool->avail_cv);
    proxy_mutex_init(new_pool->avail_mutex);

    return new_pool;
}

/**
 * Modify the size of an existing pool, allocating memory as necessary.
 *
 * \param pool Pool to resize.
 * \param size New size of the pool.
 **/
void proxy_pool_set_size(pool_t *pool, int size) {
    int alloc=1, i;
    my_bool *avail;

    if (size == pool->size)
        return;

    proxy_mutex_lock(&(pool->lock));

    /* Get the new allocated size */
    while (alloc < size)
        alloc <<= 1;

    /* Allocate a new array and copy old availability */
    if (alloc != pool->__alloc) {
        avail = (my_bool*) calloc(alloc, sizeof(my_bool));

        for (i=0; i<pool->size; i++)
            avail[i] = pool->avail[i];

        for (i=pool->size; i<alloc; i++)
            avail[i] = TRUE;

        free(pool->avail);
        pool->avail = avail;
        pool->__alloc = alloc;
    }

    pool->size = size;

    proxy_mutex_unlock(&(pool->lock));
}

/**
 * Remove an item from a pool and resize accordingly.
 *
 * \param pool Pool that the item should be removed from.
 * \param idx  Index of the item to remove.
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
 * \param pool Pool to check.
 *
 * \return Index of an available item, or negative if no items are available.
 **/
static int pool_try_locks(pool_t *pool) {
    int i;

    proxy_mutex_lock(&(pool->lock));

    /* Check availability of items in the pool */
    for (i=0; i<pool->size; i++) {
        if (pool->avail[i]) {
            pool->avail[i] = FALSE;
            proxy_mutex_unlock(&(pool->lock));
            return i;
        }
    }

    proxy_mutex_unlock(&(pool->lock));
    return -1;
}

/**
 * Get an available item from a pool, waiting if necessary.
 *
 * \param pool Pool to check.
 *
 * \return Index of an available item in the pool.
 **/
int proxy_get_from_pool(pool_t *pool) {
    int idx;

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

/**
 * Get the next item in a pool which is currently locked.
 *
 * \param pool Pool to check.
 *
 * \return Index of a locked item, or negative if no items are locked.
 **/
int proxy_pool_get_locked(pool_t *pool) {
    int i;

    proxy_mutex_lock(&(pool->lock));

    for (i=0; i<pool->size; i++) {
        if (!(pool->avail[i])) {
            proxy_mutex_unlock(&(pool->lock));
            return i;
        }
    }

    proxy_mutex_unlock(&(pool->lock));
    return -1;
}

/**
 * Return a locked item to the pool.
 *
 * \param pool Pool the item should be returned to.
 * \param idx  Index of the item to return
 **/
void proxy_return_to_pool(pool_t *pool, int idx) {
    printf("You can have %d back\n", idx);
    
    /* Update the item availability */
    proxy_mutex_lock(&(pool->lock));
    if (pool->avail[idx])
        proxy_error("Trying to free lock from already free pool");
    else
        pool->avail[idx] = TRUE;
    proxy_mutex_unlock(&(pool->lock));

    /* Signify availability in case someone is waiting */
    proxy_mutex_lock(pool->avail_mutex);
    proxy_cond_signal(pool->avail_cv);
    proxy_mutex_unlock(pool->avail_mutex);
}

/**
 * Free all memory and mutexes associated with the pool.
 *
 * \param pool Pool to destroy.
 **/
void proxy_pool_destroy(pool_t *pool) {
    /* Unlock the mutex if locked */
    proxy_mutex_trylock(&(pool->lock));
    proxy_mutex_unlock(&(pool->lock));
    proxy_mutex_destroy(&(pool->lock));

    free(pool->avail);

    proxy_cond_destroy(pool->avail_cv);
    free(pool->avail_cv);
    proxy_mutex_destroy(pool->avail_mutex);
    free(pool->avail_mutex);

    /* Finally, free the pool itself */
    free(pool);
}
