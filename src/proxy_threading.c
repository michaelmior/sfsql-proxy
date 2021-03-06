/******************************************************************************
 * proxy_threading.c
 *
 * Initialize necessary data structures for threading.
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

#include <signal.h>
#include <sys/prctl.h>

/** Signals which should be handled by the proxy */
static int handle_sigs[] = { SIGINT, SIGTERM };
/** Signal set corresponding to ::handle_sigs */
static sigset_t handle_set;

/**
 * Initialize threading data structures.
 **/
void proxy_threading_init() {
    int i, size = sizeof(handle_sigs)/sizeof(*handle_sigs);

    /* Set up signal set for masked signals */
    sigemptyset(&handle_set);
    for (i=0; i<size; i++)
        sigaddset(&handle_set, handle_sigs[i]);

    /* Initialize thread-specific data */
    pthread_key_create(&thread_buf_key, &free);

#ifdef THREADING_DEBUG
    pthread_mutexattr_init(&__proxy_mutexattr);
    pthread_mutexattr_settype(&__proxy_mutexattr, PTHREAD_MUTEX_ERRORCHECK);
#endif
}

/**
 * Give a name to a thread.
 *
 * @param name Name for the thread.
 */
void proxy_threading_name(char *name) {
    prctl(PR_SET_NAME, name, NULL, NULL, NULL);
}

/*
 * Block signals so they are handled by the main thread
 * and initialize thread-specific data.
 */
void proxy_threading_mask() {
    pthread_sigmask(SIG_BLOCK, &handle_set, NULL);
    pthread_setspecific(thread_buf_key, malloc(BUFSIZ));
}

/**
 * Free threading data structures.
 **/
void proxy_threading_end() {
    /* Delete thread-specific data keys */
    pthread_key_delete(thread_buf_key);

#ifdef THREADING_DEBUG
    pthread_mutexattr_destroy(&__proxy_mutexattr);
#endif
}

/**
 * Cancel all running client threads. This signals threads to check
 * for work, and they will exit upon seeing no work available.
 * We then return any locked threads to the pool.
 *
 * @param threads Array of threads to cancel.
 * @param num     Number of threads in the array.
 * @param pool    Pool for locking thread access.
 **/
void proxy_threading_cancel(proxy_thread_t *threads, int num, pool_t *pool) {
    int i;

    for (i=0; i<num; i++) {
        /* We've already shut down this thread */
        if (threads[i].exit)
            continue;

        threads[i].exit = 1;

        /* Make sure worker threads release their mutex */
        proxy_mutex_lock(&threads[i].lock);
        proxy_cond_signal(&threads[i].cv);
        proxy_mutex_unlock(&threads[i].lock);

        /* Try to acquire the lock again to ensure threads
         * have exited and cleaned up */
        proxy_mutex_lock(&threads[i].lock);
        proxy_mutex_unlock(&threads[i].lock);
    }

    /* Return any locked threads to the pool */
    while ((i = proxy_pool_get_locked(pool)) >= 0)
        proxy_pool_return(pool, i);
}

/**
 *  Join and clean up thread data structures.
 *
 *  @param threads Array of threads to clean up.
 *  @param num     Number of threads in the array.
 *  @param pool    Pool for locking thread access.
 **/
void proxy_threading_cleanup(proxy_thread_t *threads, int num, pool_t *pool) {
    int i;

    /* Join all threads */
    if (threads) {
        for (i=0; i<num; i++) {
            pthread_join(threads[i].thread, NULL);

            proxy_cond_destroy(&threads[i].cv);
            proxy_mutex_destroy(&threads[i].lock);
        }

        /* Free extra memory */
        free(threads);
        threads = NULL;
    }

    if (pool) {
        proxy_pool_destroy(pool);
        pool = NULL;
    }
}
