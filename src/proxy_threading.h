/*
 * proxy_threading.h
 *
 * Initialize necessary data structures for threading.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_threading_h
#define _proxy_threading_h

#include "proxy.h"

#define STACK_SIZE 65536

/**
 * Data structures needed for thread pool
 * implementation and signaling of new work.
 **/
typedef struct {
    /** Number of the thread. */
    int id;
    /** pthreads thread identifier. */
    pthread_t thread;
    /** Condition variable for signifying
        the thread of available work. */
    pthread_cond_t cv;
    /** Lock associated with condition variable. */
    pthread_mutex_t lock;
    /** Signal that that the thread should now exit. */
    volatile sig_atomic_t exit;

    /** Work for different types of threads. */
    union {
        proxy_work_t work;
        proxy_backend_data_t backend;
    } data;

    /** Commit info for the transaction associated
     *  with this thread. */
    commitdata_t *commit;

    /** Status info for the connection associated
     *  with this thread. */
    status_t *status;
} proxy_thread_t;

/** Key for retrieving thread-specific buffer */
pthread_key_t thread_buf_key;

/** Get an error message in a thread-safe way using thread-specific data as a buffer */
#define errstr strerror_r(errno, pthread_getspecific(thread_buf_key), BUFSIZ) ?: ""

void proxy_threading_init();
void proxy_threading_name(char *name);
void proxy_threading_mask();
void proxy_threading_end();
void proxy_threading_cancel(proxy_thread_t *threads, int num, pool_t *pool);
void proxy_threading_cleanup(proxy_thread_t *threads, int num, pool_t *pool);

/**
 * Wrapper for pthread_create so we can set any additional thread attributes.
 *
 * @param[out] thread   Location to store pthreads thread ID.
 * @param attr          Thread-specific attributes.
 * @param start_routine Function to execute in the new thread.
 * @param arg           Parameter passed to start_routine.
 *
 * @return int          Same as pthread_create.
 **/
static inline int proxy_threading_create(pthread_t *restrict thread, pthread_attr_t *restrict attr,
        void *(*start_routine)(void*), void *restrict arg) {
    if (attr)
        pthread_attr_setstacksize(attr, STACK_SIZE);

    return pthread_create(thread, attr, start_routine, arg);
}

/* Debugging macros for catching errors on mutexes and condition variables*/

#ifdef THREADING_DEBUG
#define __proxy_error(loc, str) proxy_log(LOG_DEBUG, str " at :%s", loc)
#endif

#ifdef THREADING_DEBUG
pthread_mutexattr_t __proxy_mutexattr;

static inline int __proxy_mutex_init(pthread_mutex_t *m, char *loc) {
    int ret = pthread_mutex_init(m, &__proxy_mutexattr);
    switch (ret) {
        case 0:
            break;
        case EAGAIN:
            __proxy_error(loc, "No resources for initializing mutex");
            break;
        case ENOMEM:
            __proxy_error(loc, "No memory for initializing mutex");
            break;
        case EPERM:
            __proxy_error(loc, "Invalid privilege to initialize mutex");
            break;
    }
    return ret;
}
#define proxy_mutex_init(m) __proxy_mutex_init(m, AT)
#else
#define proxy_mutex_init(m) pthread_mutex_init(m, NULL)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_mutex_destroy(pthread_mutex_t *m, char *loc) {
    int ret = pthread_mutex_destroy(m);
    switch (ret) {
        case 0:
            break;
        case EBUSY:
            __proxy_error(loc, "Destroying locked mutex");
            break;
        case EINVAL:
            __proxy_error(loc, "Destroying invalid mutex");
            break;
    }
    return ret;
}
#define proxy_mutex_destroy(m) __proxy_mutex_destroy(m, AT)
#else
#define proxy_mutex_destroy(m) pthread_mutex_destroy(m)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_get_mutex(pthread_mutex_t *m, int(*func)(pthread_mutex_t*), char *loc) {
    int ret = func(m);
    switch (ret) {
        case 0:
            break;
        case EBUSY:
            if (func != pthread_mutex_trylock)
                __proxy_error(loc, "Locking already locked mutex");
            break;
        case EINVAL:
            __proxy_error(loc, "Locking uninitialized mutex");
            break;
        case EFAULT:
            __proxy_error(loc, "Locking mutex with invalid pointer");
            break;
        case EDEADLK:
            __proxy_error(loc, "Relocking already held mutex");
            break;
    }
    return ret;
}
#define proxy_mutex_trylock(m) __proxy_get_mutex(m, pthread_mutex_trylock, AT)
#define proxy_mutex_lock(m) __proxy_get_mutex(m, pthread_mutex_lock, AT)
#else
#define proxy_mutex_trylock(m) pthread_mutex_trylock(m)
#define proxy_mutex_lock(m) pthread_mutex_lock(m)
#endif


#ifdef THREADING_DEBUG
static inline int __proxy_mutex_unlock(pthread_mutex_t *m, char *loc) {
    int ret = pthread_mutex_unlock(m);
    switch (ret) {
        case 0:
            break;
        case EINVAL:
            __proxy_error(loc, "Unlocking uninitialized mutex");
            break;
        case EFAULT:
            __proxy_error(loc, "Unlocking mutex with invalid pointer");
            break;
        case EPERM:
            __proxy_error(loc, "Unlocking unowned mutex");
            break;
    }
    return ret;
}
#define proxy_mutex_unlock(m) __proxy_mutex_unlock(m, AT)
#else
#define proxy_mutex_unlock(m) pthread_mutex_unlock(m)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_cond_init(pthread_cond_t *cv, char *loc) {
    int ret = pthread_cond_init(cv, NULL);
    switch (ret) {
        case 0:
            break;
        case EAGAIN:
            __proxy_error(loc, "No resources to initialize condition variable");
            break;
        case ENOMEM:
            __proxy_error(loc, "No memory for initializing condition variable");
            break;
        case EBUSY:
            __proxy_error(loc, "Reinitializing condition variable in use");
            break;
        case EINVAL:
            __proxy_error(loc, "Invalid condition variable attribute");
            break;
    }
    return ret;
}
#define proxy_cond_init(cv) __proxy_cond_init(cv, AT)
#else
#define proxy_cond_init(cv) pthread_cond_init(cv, NULL)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_cond_destroy(pthread_cond_t *cv, char *loc) {
    int ret = pthread_cond_destroy(cv);
    switch (ret) {
        case 0:
            break;
        case EBUSY:
            __proxy_error(loc, "Trying to destroy condition variable in use");
            break;
        case EINVAL:
            __proxy_error(loc, "Trying to destroy invalid condition variable");
            break;
    }
    return ret;
}
#define proxy_cond_destroy(cv) __proxy_cond_destroy(cv, AT)
#else
#define proxy_cond_destroy(cv) pthread_cond_destroy(cv)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_cond_signal(pthread_cond_t *cv, int(*func)(pthread_cond_t*), char *loc) {
    int ret = func(cv);
    switch (ret) {
        case 0:
            break;
        case EINVAL:
            __proxy_error(loc, "Trying to signal invalid condition variable");
            break;
    }
    return ret;
}
#define proxy_cond_signal(cv) __proxy_cond_signal(cv, pthread_cond_signal, AT)
#define proxy_cond_broadcast(cv) __proxy_cond_signal(cv, pthread_cond_broadcast, AT)
#else
#define proxy_cond_signal(cv) pthread_cond_signal(cv)
#define proxy_cond_broadcast(cv) pthread_cond_broadcast(cv)
#endif

#ifdef THREADING_DEBUG
static inline int __proxy_cond_wait(pthread_cond_t *cv, pthread_mutex_t *m, int(*func)(pthread_cond_t*, pthread_mutex_t*), char *loc) {
    int ret = func(cv, m);
    switch (ret) {
        case 0:
            break;
        case EINVAL:
            __proxy_error(loc, "Error waiting on condition variable");
            break;
        case ETIMEDOUT:
            __proxy_error(loc, "Timeout waiting for condition variable");
            break;
    }
    return ret;
}
#define proxy_cond_wait(cv, m) __proxy_cond_wait(cv, m, pthread_cond_wait, AT)
#define proxy_cond_timedwait(cv, m) __proxy_cond_wait(cv, m, pthread_cond_timedwait, AT)
#else
#define proxy_cond_wait(cv, m) pthread_cond_wait(cv, m)
#define proxy_cond_timedwait(cv, m) pthread_cond_timedwait(cv, m)
#endif

#endif /* _proxy_threading_h */
