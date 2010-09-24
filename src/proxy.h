/*
 * proxy.h
 *
 * Main proxy include file with useful macros, structs, and default configuration options
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_h
#define _proxy_h

#include <my_global.h>

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include "config.h"

#include <my_sys.h>
#include <m_string.h>
#include <m_ctype.h>
#include <mysql_com.h>
#include <violite.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <mysql.h>
#include <pthread.h>

extern volatile sig_atomic_t cloning;  /** Signify that we are currently cloning */

/**
 *  Output an error message to stderr
 *
 * \param loc The location in the code the error is from, normally generated by the ::AT macro.
 * \param fmt A printf-style format string (remaining arguments are format parameters).
 **/
#ifdef DEBUG
static inline void __proxy_error(const char *loc, ...) {
#else
static inline void __proxy_error(__attribute__((unused)) void *p, ...) {
#endif
    char *fmt;
    va_list arg;

    va_start(arg, NULL);
    fmt = va_arg(arg, char*);

    vfprintf(stderr, fmt, arg);
    va_end(arg);

#ifdef DEBUG
    fprintf(stderr, " at %s\n", loc);
#else
    fprintf(stderr, "\n");
#endif
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

#ifdef DEBUG
#define proxy_error(...) __proxy_error(AT, __VA_ARGS__)
#else
#define proxy_error(...) __proxy_error(NULL, __VA_ARGS__)
#endif

#define errstr strerror_r(errno, BUF, BUFSIZ)

#include "proxy_net.h"
#include "proxy_backend.h"
#include "proxy_pool.h"
#include "proxy_threading.h"
#include "proxy_options.h"

pool_t *thread_pool;

/**
 * Copied from client/sql_string.h since this
 * function is not included in the client library. */
uint32 copy_and_convert(char *to, uint32 to_length, CHARSET_INFO *to_cs,
            const char *from, uint32 from_length,
            CHARSET_INFO *from_cs, uint *errors);

#endif /* _proxy_h */
