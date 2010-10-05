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

#define LOG_FILE "/var/log/sfsql-proxy.log"

typedef enum {
    LOG_ERROR,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

void proxy_log_open();
void proxy_log(log_level_t level, char *fmt, ...);
void proxy_log_close();
