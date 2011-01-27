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

/** Thread identifier of the monitor thread */
static pthread_t monitor_thread;
/** Signifies if the monitor thread was
 *  started in this session */
static my_bool monitor_started = FALSE;

/**
 * Monitor thread function.
 *
 * @param ptr Not currently used.
 *
 * @return NULL.
 **/
static void* monitor_thread_start(__attribute__((unused)) void *ptr) {
    FILE *stat_file;
    struct timeval tv;

    proxy_threading_name("Monitor");

    /* Wait for the server to be started */
    while (!run) { usleep(SYNC_SLEEP); }

    /* Connect to the master */
    if (options.coordinator)
        monitor_master_connect();

    /* Check if we are dumping QPS statistics and
     * try to open the statistics file */
    if (!options.stat_file)
        goto out;

    stat_file = fopen(options.stat_file, "w");
    if (!stat_file) {
        proxy_log(LOG_ERROR, "Error opening statistics file");
        goto out;
    }
    proxy_log(LOG_INFO, "Statistics file %s opened for output", options.stat_file);

    /* Loop while the proxy is running and dump
     * total number of executed queries */
    /* XXX: global_status is currently updated
     *      only when a connection is killed,
     *      so statistics will be stale if
     *      connections are long-lived */
    while (run) {
        gettimeofday(&tv, NULL);
        fprintf(stat_file, "%ld.%06ld,%ld\n", tv.tv_sec, tv.tv_usec, global_status.queries);
#ifdef DEBUG
        fflush(stat_file);
        fsync(fileno(stat_file));
#endif
        sleep(1);
    }
    fclose(stat_file);

out:
    mysql_thread_end();
    pthread_exit(NULL);
}

/**
 * Prepare monitoring and start the monitor thread.
 *
 * @return TRUE on error, FALSE otherwise.
 **/
my_bool proxy_monitor_init() {
    if (!pthread_create(&monitor_thread, NULL, monitor_thread_start, NULL))
        monitor_started = TRUE;

    return !monitor_started;
}

/**
 * Shutdown monitoring.
 **/
void proxy_monitor_end() {
    /* Wait for the monitor thread to exit */
    if (monitor_started)
        pthread_join(monitor_thread, NULL);
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
        proxy_log(LOG_INFO, "Setting coordinator host to %s:%d on master", options.phost, options.admin_port);
        snprintf(buff, BUFSIZ, "PROXY COORDINATOR %s:%d;", options.phost, options.admin_port);
        if (mysql_query(master, buff)) {
            proxy_log(LOG_ERROR, "Couldn't set coordinator on master host: %s", mysql_error(master));
            return TRUE;
        }
    }

    proxy_log(LOG_INFO, "Successfully connected to master");
    return FALSE;
}

