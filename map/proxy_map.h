#ifndef _proxy_query_h
#define _proxy_query_h

#include <string.h>

/**
 * Types of query mappings.
 **/
enum QUERY_MAP {
    QUERY_MAP_ANY, /** Map to any available backend. */
    QUERY_MAP_ALL  /** Map to all backends. */
};

/**
 * Information about mapping for a
 * particular query. */
typedef struct {
    enum QUERY_MAP map;
    char *query;
} proxy_query_map_t;

/** Function pointer for query mapping. */
typedef proxy_query_map_t* (*proxy_map_query_t) (char*);

#endif /* _proxy_query_h */
