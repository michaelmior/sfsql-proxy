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

#define strprefix(str, cmp, len) (((sizeof(cmp) - 1) > len) ? 0 : (strncasecmp(str, cmp, sizeof(cmp)-1) == 0))

proxy_query_map_t proxy_map_query(char *query, unsigned long *query_len, char **new_query) {
    if (new_query)
        *new_query = NULL;

    /* Anything which starts with the keywords above
     * goes to any backend, otherwise, go everywhere */
    if (strprefix(query, "SELECT", *query_len)
        || strprefix(query, "SHOW", *query_len)
        || strprefix(query, "DESCRIBE", *query_len)
        || strprefix(query, "EXPLAIN", *query_len))
        return QUERY_MAP_ANY;
    else
        return QUERY_MAP_ALL;
}
