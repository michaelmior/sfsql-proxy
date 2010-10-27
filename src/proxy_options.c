/******************************************************************************
 * proxy_options.c
 *
 * Proxy command-line options parsing
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

#include <getopt.h>
#include <sysexits.h>

#include "proxy.h"

static char BUF[BUFSIZ];

/**
 * Print a simple usage message with command-line arguments.
 **/
static void usage() {
    printf(
            "SnowFlock SQL proxy server - (C) Michael Mior <mmior@cs.toronto.edu>, 2010\n\n"

            "Options:\n"
            "\t--help,             -?\tShow this message\n"
            "\t--daemonize,        -d\tDaemonize\n\n"

            "Backend options:\n"
            "\t--backend-host,    -h\tHost to forward queries to (default: 127.0.0.1)\n"
            "\t--backend-port,    -P\tPort of the backend host (default: 3306)\n"
            "\t--socket,          -s\tUse a UNIX socket for the backend connection\n\n"

            "\t--backend-db,      -D\tName of database on the backend (default: test)\n"
            "\t--backend-user,    -u\tUser for backend server (default: root)\n"
            "\t--backend-pass,    -p\tPassword for backend user\n\n"
            "\t--backend-file,    -f\tFile listing available backends\n"
            "\t                     \t(cannot be specified with above options)\n\n"
            "\t--num-conns,       -N\tNumber connections per backend\n"
            "\t                   -a\tDisable autocommit (default is enabled)\n"
            "\t--add-ids,         -i\tTag transactions with unique identifiers\n\n"

            "Proxy options:\n"
            "\t--proxy-host,      -b\tBinding address (default is 0.0.0.0)\n"
            "\t--proxy-port,      -L\tPort for the proxy server to listen on (default: 4040)\n"
            "\t--timeout,         -n\tSeconds to wait wihout data before disconnecting clients,\n"
            "\t                     \tnegative to wait forever (default: 5)\n\n"

            "Mapper options:\n"   
            "\t--mapper,          -m\tMapper to use for mapping queryies to backends\n"
            "\t                     \t(default is first available)\n\n"

            "Thread options:\n"
            "\t--client-threads,  -t\tNumber of threads to handle client connections\n"
            "\t--backend-threads, -T\tNumber of threads to dispatch backend queries\n\n"
    );
}

/**
 * Parse command-line options
 *
 * \param argc Number of arguments.
 * \param argv Argument list.
 **/
int parse_options(int argc, char *argv[]) {
    int c, opt=0;
    static struct option long_options[] = {
        {"help",            no_argument,       0, '?'},
        {"daemonize",       no_argument,       0, 'd'},
        {"backend-host",    required_argument, 0, 'h'},
        {"backend-port",    required_argument, 0, 'P'},
        {"socket",          optional_argument, 0, 's'},
        {"backend-db",      required_argument, 0, 'D'},
        {"backend-user",    required_argument, 0, 'u'},
        {"backend-pass",    required_argument, 0, 'p'},
        {"backend-file",    required_argument, 0, 'f'},
        {"num-conns",       required_argument, 0, 'N'},
        {"add-ids",         no_argument,       0, 'i'},
        {"proxy-host",      required_argument, 0, 'b'},
        {"proxy-port",      required_argument, 0, 'L'},
        {"timeout",         required_argument, 0, 'n'},
        {"mapper",          required_argument, 0, 'm'},
        {"client-threads",  required_argument, 0, 't'},
        {"backend-threads", required_argument, 0, 'T'},
        {0, 0, 0, 0}
    };

    /* Set options to default values */
    options.help            = FALSE;
    options.daemonize       = FALSE;

    options.num_conns       = NUM_CONNS;
    options.add_ids         = FALSE;
    options.autocommit      = TRUE;
    options.backend.host    = NULL;
    options.backend.port    = 0;
    options.unix_socket     = FALSE;
    options.socket_file     = NULL;
    options.user            = NULL;
    options.pass            = NULL;
    options.db              = NULL;
    options.backend_file    = NULL;
    options.phost           = NULL;
    options.pport           = PROXY_PORT;
    options.timeout         = CLIENT_TIMEOUT;
    options.mapper          = NULL;
    options.client_threads  = CLIENT_THREADS;
    options.backend_threads = BACKEND_THREADS; 

    /* Parse command-line options */
    while((c = getopt_long(argc, argv, "?dh:P:s::n:D:u:p:f:N:iaAb:L:m:t:T:", long_options, &opt)) != -1) {
        switch(c) {
            case '?':
                options.help = TRUE;
                usage();
                return EXIT_SUCCESS;
            case 'd':
                options.daemonize = TRUE;
                break;
            case 'h':
                options.backend.host = optarg;
                break;
            case 'P':
                options.backend.port = atoi(optarg);
                break;
            case 's':
                options.unix_socket = TRUE;
                if (optarg)
                    options.socket_file = optarg;
                else
                    options.socket_file = MYSQL_UNIX_ADDR;
                break;
            case 'n':
                options.timeout = atoi(optarg);
                break;
            case 'D':
                options.db = optarg;
                break;
            case 'u':
                options.user = optarg;
                break;
            case 'p':
                options.pass = optarg;
                break;
            case 'f':
                options.backend_file = optarg;
                break;
            case 'N':
                options.num_conns = atoi(optarg);
                break;
            case 'i':
                options.add_ids = TRUE;
                break;
            case 'a':
                options.autocommit = FALSE;
                break;
            case 'b':
                options.phost = optarg;
                break;
            case 'L':
                options.pport = atoi(optarg);
                break;
            case 'm':
                options.mapper = optarg;
                break;
            case 't':
                options.client_threads = atoi(optarg);
                break;
            case 'T':
                options.backend_threads = atoi(optarg);
                break;
            default:
                usage();
                return EX_USAGE;
        }

        opt = 0;
    }

    /* Set defaults for unspecified options */
    options.user = options.user ? options.user : BACKEND_USER;
    options.pass = options.pass ? options.pass : BACKEND_PASS;
    options.db   = options.db   ? options.db   : BACKEND_DB;

    if (options.backend_file) {
        if (options.backend.host || options.backend.port || options.unix_socket) {
            usage();
            return EX_USAGE;
        } else if (access(options.backend_file, R_OK)) {
            proxy_log(LOG_ERROR, "Error accessing backend file %s:%s", options.backend_file, errstr);
            return EX_NOINPUT;
        }
    } else {
        if ((options.backend.host || options.backend.port) && options.unix_socket) {
            usage();
            return EX_USAGE;
        }

        options.backend.host = options.backend.host ?
            options.backend.host : BACKEND_HOST;
        options.backend.port = options.backend.port ?
            options.backend.port : BACKEND_PORT;
    }
    
    return EXIT_SUCCESS;
}
