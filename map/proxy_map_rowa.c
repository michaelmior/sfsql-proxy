#include "proxy_map.h"

#define SELECT "SELECT"

proxy_query_map_t* proxy_map_query(char *query) {
    proxy_query_map_t *map = NULL;
    
    map = (proxy_query_map_t*) malloc(sizeof(proxy_query_map_t));
    if (!map)
        return NULL;

    map->query = strdup(query);

    /* Anything which starts with SELECT goes to
     * any backend, otherwise, go everywhere */
    if (strncmp(query, SELECT, strlen(SELECT)) == 0)
        map->map = QUERY_MAP_ANY;
    else
        map->map = QUERY_MAP_ALL;

    return map;
}
