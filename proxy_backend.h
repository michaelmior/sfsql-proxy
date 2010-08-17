#ifndef _proxy_backend_h
#define _proxy_backend_h

int proxy_backend_connect();
my_bool proxy_backend_query(MYSQL *proxy, const char *query, ulong length);
void proxy_backend_close();

#endif /* _proxy_backend_h */
