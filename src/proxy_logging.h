/*
 * proxy_logging.h
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

typedef enum {
    LOG_ERROR,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

void proxy_log_open();
void proxy_log(log_level_t level, const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));
void proxy_log_close();

#endif /* _proxy_logging_h */
