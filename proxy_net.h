#ifndef _proxy_net_h
#define _proxy_net_h

#include <my_base.h>

void proxy_handshake(MYSQL *mysql, struct sockaddr_in *clientaddr, int thread_id);
my_bool proxy_check_user(char *user, uint user_len, char *passwd, uint passwd_len, char *db, uint db_len);
my_bool proxy_read_query(MYSQL *mysql);
my_bool proxy_send_ok(MYSQL *mysql, uint status, uint warnings, ha_rows affected_rows, ulonglong last_insert_id);

#endif /* _proxy_net_h */
