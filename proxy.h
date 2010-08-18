#ifndef _proxy_h
#define _proxy_h

#include <my_global.h>
#include <my_sys.h>
#include <m_string.h>
#include <m_ctype.h>
#include <mysql_com.h>
#include <violite.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <mysql.h>

#include "proxy_net.h"
#include "proxy_backend.h"

#define DEBUG 1

#define BACKEND_HOST "127.0.0.1"
#define BACKEND_PORT 3306
#define BACKEND_USER "root"
#define BACKEND_PASS "root"
#define BACKEND_DB   "test"

#define PROXY_PORT   4040

//extern MYSQL *mysql;

void proxy_error(const char *fmt, ...);

/* from client/sql_string.h */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
