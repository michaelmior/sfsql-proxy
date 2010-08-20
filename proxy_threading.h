void proxy_threading_init();
void proxy_threading_end();


#ifdef DEBUG
pthread_mutexattr_t __proxy_mutexattr;

#define proxy_mutex_init(m) \
    switch (pthread_mutex_init(m, &__proxy_mutexattr)) { \
        case 0: \
            break; \
        case EAGAIN: \
            proxy_error("No resources for initializing mutex"); \
            break; \
        case ENOMEM: \
            proxy_error("No memory for initializing mutex"); \
            break; \
        case EPERM: \
            proxy_error("Invalid privilege to initialize mutex"); \
            break; \
    }
#else
#define proxy_mutex_init(m) pthread_mutex_init(m, NULL) 
#endif

#ifdef DEBUG
#define proxy_mutex_destroy(m) \
    switch (pthread_mutex_destroy(m)) { \
        case 0: \
            break; \
        case EBUSY: \
            proxy_error("Destroying locked mutex"); \
            break; \
        case EINVAL: \
            proxy_error("Destroying invalid mutex"); \
            break; \
    } 
#endif

#ifdef DEBUG
#define __proxy_get_mutex(m, type) \
    switch (pthread_mutex_##type(m)) { \
        case 0: \
            break; \
        case EBUSY: \
            proxy_error("Locking already locked mutex"); \
            break; \
        case EINVAL: \
            proxy_error("Locking uninitialized mutex"); \
            break; \
        case EFAULT: \
            proxy_error("Locking mutex with invalid pointer"); \
            break; \
        case EDEADLK: \
            proxy_error("Relocking already held mutex"); \
            break; \
    }
#else
#define __proxy_get_mutex(m, type) pthread_mutex_##type(m)
#endif

#define proxy_mutex_trylock(m) __proxy_get_mutex(m, trylock)
#define proxy_mutex_lock(m) __proxy_get_mutex(m, lock)

#ifdef DEBUG
#define proxy_mutex_unlock(m) \
    switch (pthread_mutex_unlock(m)) { \
        case 0: \
            break; \
        case EINVAL: \
            proxy_error("Unlocking uninitialized mutex"); \
            break; \
        case EFAULT: \
            proxy_error("Unlocking mutex with invalid pointer"); \
            break; \
        case EPERM: \
            proxy_error("Unlocking mutex without lock"); \
            break; \
    }
#else
#define proxy_mutex_unlock(m) pthread_mutex_unlock(m)
#endif
