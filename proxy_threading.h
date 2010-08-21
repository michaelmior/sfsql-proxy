/* 
 * proxy_threading.h
 *
 * Hide pthreads functions to provide useful debugging
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

void proxy_threading_init();
void proxy_threading_end();

#ifdef DEBUG
pthread_mutexattr_t __proxy_mutexattr;

int static inline __proxy_mutex_init(pthread_mutex_t *m, char *loc) {
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

#ifdef DEBUG
int static inline __proxy_mutex_destroy(pthread_mutex_t *m, char *loc) {
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

#ifdef DEBUG
int static inline __proxy_get_mutex(pthread_mutex_t *m, int(*func)(pthread_mutex_t*), char *loc) {
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


#ifdef DEBUG
int static inline __proxy_mutex_unlock(pthread_mutex_t *m, char *loc) {
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
            __proxy_error(loc, "Unlocking mutex without lock"); 
            break; 
    }
    return ret;
}
#define proxy_mutex_unlock(m) __proxy_mutex_unlock(m, AT)
#else
#define proxy_mutex_unlock(m) pthread_mutex_unlock(m)
#endif

#ifdef DEBUG
int static inline __proxy_cond_init(pthread_cond_t *cv, char *loc) {
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

#ifdef DEBUG
int static inline __proxy_cond_destroy(pthread_cond_t *cv, char *loc) {
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

#ifdef DEBUG
int static inline __proxy_cond_signal(pthread_cond_t *cv, int(*func)(pthread_cond_t*), char *loc) {
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

#ifdef DEBUG
int static inline __proxy_cond_wait(pthread_cond_t *cv, pthread_mutex_t *m, int(*func)(pthread_cond_t*, pthread_mutex_t*), char *loc) {
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