/******************************************************************************
 * proxy_logging.c
 *
 * Message logging
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

FILE *log_file = NULL;
log_level_t log_level;

/**
 * Open the log file.
 * 
 * \param level Maximum level of logged messages.
 **/
void proxy_log_open() {
    /* Save the log level */
#ifdef DEBUG
    log_level = LOG_DEBUG;
#else
    log_level = LOG_INFO;
#endif

    /* Set the log file */
    if (options.daemonize)
        log_file = fopen(LOG_FILE, "w");

    if (!log_file)
        log_file = stderr;
}

/**
 *
 * \param level Log level.
 * \param fmt   Format string.
 **/
void proxy_log(log_level_t level, char *fmt, ...) {
    va_list arg;

    /* Check if the message should be logged */
    if (level > log_level)
        return;

    /* Write the error message */
    va_start(arg, fmt);
    vfprintf(log_file, fmt, arg);
    va_end(arg);

    fwrite("\n", 1, 1, log_file);

#ifdef DEBUG
    fflush(log_file);
    fsync(fileno(log_file));
#endif
}

/**
 * Close the log file.
 **/
void proxy_log_close() {
    if (log_file) {
        fflush(log_file);
        fsync(fileno(log_file));
        fclose(log_file);
    }
}
