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
