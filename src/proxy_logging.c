/******************************************************************************
 * proxy_logging.c
 *
 * Message logging.
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

#include "proxy.h"

#include <stdio.h>
#include <stdarg.h>

/** File object corresponding to info log file descriptor */
static FILE *info_log = NULL;
/** File object corresponding to error log file descriptor */
static FILE *err_log  = NULL;

/** Maximum level of messages to log */
log_level_t log_level;

/**
 * Open the log file.
 **/
my_bool proxy_log_open() {
    /* Save the log level */
#ifdef DEBUG
    log_level = LOG_DEBUG;
#else
    log_level = LOG_INFO;
#endif

    /* Set the log file */
    if (options.daemonize) {
        info_log = fopen(LOG_FILE, "w");
        err_log  = info_log;

        if (!info_log) {
            fprintf(stderr, "Error opening log file %s\n", LOG_FILE);
            return TRUE;
        }
    }

    if (!info_log) {
        info_log = stdout;
        err_log  = stderr;
    }

    return FALSE;
}

/**
 * Log a message to the previously specified log file.
 *
 * @param level Log level.
 * @param fmt   Format string.
 **/
void _proxy_log(log_level_t level, const char *fmt, ...) {
    va_list arg;
    FILE *log_file = (level == LOG_ERROR) ? err_log : info_log;

    /* Check if the message should be logged */
    if (level > log_level)
        return;

    /* Write the error message */
    va_start(arg, fmt);
    vfprintf(log_file, fmt, arg);
    va_end(arg);

#ifdef DEBUG
    fflush(log_file);
    fsync(fileno(log_file));
#endif
}

/**
 * Close the log file.
 **/
void proxy_log_close() {
    /* Close both log files */
#define CLOSE_LOG(log_file) \
    if (log_file) { \
        fflush(log_file); \
        fsync(fileno(log_file)); \
        fclose(log_file); \
    }

    CLOSE_LOG(info_log);
    CLOSE_LOG(err_log);

#undef CLOSE_LOG
}
