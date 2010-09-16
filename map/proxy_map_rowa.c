/******************************************************************************
 * proxy_map_rowa.c
 *
 * Read one, write all query mapper
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

#include <stdlib.h>

#include "proxy_map.h"

#define SELECT   "SELECT"
#define SHOW     "SHOW"
#define DESCRIBE "DESCRIBE"
#define EXPLAIN  "EXPLAIN"

proxy_query_map_t* proxy_map_query(char *query) {
    proxy_query_map_t *map = NULL;
    
    map = (proxy_query_map_t*) malloc(sizeof(proxy_query_map_t));
    if (!map)
        return NULL;

    map->query = NULL;

    /* Anything which starts with SELECT goes to
     * any backend, otherwise, go everywhere */
    if (strncasecmp(query, SELECT, strlen(SELECT)) == 0
        || strncasecmp(query, SHOW, strlen(SHOW)) == 0
        || strncasecmp(query, DESCRIBE, strlen(DESCRIBE)) == 0
        || strncasecmp(query, EXPLAIN, strlen(EXPLAIN)) == 0)
        map->map = QUERY_MAP_ANY;
    else
        map->map = QUERY_MAP_ALL;

    return map;
}
