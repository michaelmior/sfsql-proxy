/* 
 * proxy_map.h
 *
 * Interface for mapping queries to backends.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_map_h
#define _proxy_map_h

#include <string.h>

/**
 * Types of query mappings.
 **/
enum QUERY_MAP {
    /** Map to any available backend. */
    QUERY_MAP_ANY,
    /** Map to all backends. */
    QUERY_MAP_ALL
};

/**
 * Information about mapping for a
 * particular query. */
typedef struct {
    /** Result of query mapping. */
    enum QUERY_MAP map;
    /** Modified query, or NULL if unchanged. */
    char *query;
} proxy_query_map_t;

/** Function pointer for query mapping. */
typedef proxy_query_map_t* (*proxy_map_query_t) (char*);

#endif /* _proxy_map_h */
