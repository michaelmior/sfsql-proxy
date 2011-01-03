/*
 * proxy_logging.h
 *
 * Message logging.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _proxy_logging_h
#define _proxy_logging_h

#define LOG_FILE "/var/log/sfsql-proxy.log"

/** Level of message to log. */
typedef enum {
    /** Errors which are always logged. */
    LOG_ERROR,
    /** Informational messages. */
    LOG_INFO,
    /** Only used when DEBUG is defined. */
    LOG_DEBUG
} log_level_t;

my_bool proxy_log_open();
void _proxy_log(log_level_t level, const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));
void proxy_log_close();

/** Convenience macro to add newline to format strings.
 *
 *  Any non-const format strings can use ::_proxy_log instead.
 **/
#define proxy_log(level, fmt, ...) _proxy_log(level, fmt"\n", ##__VA_ARGS__)

/* Macro to disable debug messages when DEBUG is not defined */
#ifdef DEBUG
#define proxy_debug(fmt, ...) proxy_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define proxy_vdebug(fmt, ...) if (options.verbose) { proxy_log(LOG_DEBUG, fmt, ##__VA_ARGS__); }
#else
#define proxy_debug(fmt, ...) do {} while(0)
#define proxy_vdebug(fmt, ...) do {} while(0)
#endif

#endif /* _proxy_logging_h */
