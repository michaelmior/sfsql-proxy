/******************************************************************************
 * proxy_monitor.c
 *
 * Monitor the load on the proxy server and perform cloning as necessary.
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

#include <netdb.h>

static my_bool monitor_master_connect();
static void *monitor_thread_start(void *ptr);

extern volatile sig_atomic_t run;

/**
 * Monitor thread function.
 *
 * @param ptr Not currently used.
 *
 * @return NULL.
 **/
static void* monitor_thread_start(__attribute__((unused)) void *ptr) {
    /* Wait for the server to be started */
    while (!run) { usleep(SYNC_SLEEP); }

    /* Connect to the master */
    monitor_master_connect();

    mysql_thread_end();
    pthread_exit(NULL);
}

/**
 * Prepare monitoring and start the monitor thread.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_monitor_init() {
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor_thread_start, NULL);

    return FALSE;
}

/**
 * Open a connection to the master and set
 * the current host as the coordinator.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool monitor_master_connect() {
    char buff[BUFSIZ];

    /* Open a MySQL connection to the master host */
    master = mysql_init(NULL);

    if (!master) {
        proxy_log(LOG_ERROR, "Could not intialize master MySQL object");
        return TRUE;
    } else {
        my_bool reconnect = TRUE;
        MYSQL *ret;

        mysql_options(master, MYSQL_OPT_RECONNECT, &reconnect);
        ret = mysql_real_connect(master, options.backend.host, options.user,
                options.pass, NULL, options.backend.port, NULL, 0);

        if (!ret) {
            proxy_log(LOG_ERROR, "Unable to connect to master: %s", mysql_error(master));
            return TRUE;
        }

        /* Construct and send our hostname to the master */
        proxy_log(LOG_INFO, "Setting coordinator host to %s:%d on master", options.phost, options.pport);
        snprintf(buff, BUFSIZ, "PROXY COORDINATOR %s:%d;", options.phost, options.pport);
        if (mysql_query(master, buff)) {
            proxy_log(LOG_ERROR, "Couldn't set coordinator on master host: %s", mysql_error(master));
            return TRUE;
        }
    }

    proxy_log(LOG_INFO, "Successfully connected to master");
    return FALSE;
}

