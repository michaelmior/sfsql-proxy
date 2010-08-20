#include "proxy.h"

void proxy_threading_init() {
#ifdef DEBUG
    pthread_mutexattr_init(&__proxy_mutexattr);
    pthread_mutexattr_settype(&__proxy_mutexattr, PTHREAD_MUTEX_ERRORCHECK);
#endif
}

void proxy_threading_end() {
#ifdef DEBUG
    pthread_mutexattr_destroy(&__proxy_mutexattr);
#endif
}
