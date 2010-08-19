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
        pthread_mutex_init(&(new_pool->locks[i]), NULL);

    pthread_cond_init(new_pool->avail_cv, NULL);
    pthread_mutex_init(new_pool->avail_mutex, NULL);

    return new_pool;
}

static int pool_try_locks(pool_t *pool) {
    int i;

    /* Check if any object in the pool is already available */
    for (i=0; i<pool->size; i++) {
        if (pthread_mutex_trylock(&(pool->locks[i])) == 0)
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
        pthread_mutex_lock(pool->avail_mutex);
        pthread_cond_wait(pool->avail_cv, pool->avail_mutex);

        idx = pool_try_locks(pool);

        pthread_mutex_unlock(pool->avail_mutex);

        if (idx >= 0)
            return idx;
    }
}

/* Get the next item in the pool which is currently locked */
int proxy_pool_get_locked(pool_t *pool) {
    int i;

    for (i=0; i<pool->size; i++) {
        if (pthread_mutex_trylock(&(pool->locks[i])) == EBUSY)
            return i;

        pthread_mutex_unlock(&(pool->locks[i]));
    }

    return -1;
}

void proxy_return_to_pool(pool_t *pool, int idx) {
    printf("You can have %d back\n", idx);
    
    /* Unlock the associated mutex */
    pthread_mutex_unlock(&(pool->locks[idx]));

    /* Someone is waiting for something in the pool,
     * let them know something is available */
    if (pthread_mutex_trylock(pool->avail_mutex)  == EBUSY) {
        printf("Waiting for a thread? Here you go!\n");
        pthread_cond_signal(pool->avail_cv);
        pthread_mutex_unlock(pool->avail_mutex);
    }
}

void proxy_pool_destroy(pool_t *pool) {
    int i;

    /* Free all the mutexes */
    for(i=0; i<pool->size; i++)
        pthread_mutex_destroy(&(pool->locks[i]));
    free(pool->locks);

    pthread_cond_destroy(pool->avail_cv);
    free(pool->avail_cv);
    pthread_mutex_destroy(pool->avail_mutex);
    free(pool->avail_mutex);

    /* Finally, free the pool itself */
    free(pool);
}
