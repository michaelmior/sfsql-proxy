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
            "\t--help,         -?\tShow this message\n\n"
            "Backend options:\n"
            "\t--backend-host, -h\tHost to forward queries to (default: 127.0.0.1)\n"
            "\t--backend-port, -P\tPort of the backend host (default: 3306)\n"
            "\t--backend-db,   -D\tName of database on the backend (default: test)\n"
            "\t--backend-user, -u\tUser for backend server (default: root)\n"
            "\t--backend-pass, -p\tPassword for backend user\n"
            "\t--backend-file, -f\tFile listing available backends\n"
            "\t                  \t(cannot be specified with above options)\n"
            "\t--num-conns,    -N\tNumber connections per backend\n"
            "\t                -a\tDisable autocommit (default is enabled)\n\n"
            "Proxy options:\n"
            "\t--proxy-host,   -b\tBinding address (default is 0.0.0.0)\n"
            "\t--proxy-port,   -L\tPort for the proxy server to listen on (default: 4040)\n\n"
            "Mapper options:\n"
            "\t--mapper,       -m\tMapper to use for mapping queryies to backends\n"
            "\t                  \t(default is first available)\n\n"
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
        {"help",         no_argument,       0, '?'},
        {"backend-host", required_argument, 0, 'h'},
        {"backend-port", required_argument, 0, 'P'},
        {"backend-db",   required_argument, 0, 'D'},
        {"backend-user", required_argument, 0, 'u'},
        {"backend-pass", required_argument, 0, 'p'},
        {"backend-file", required_argument, 0, 'f'},
        {"num-conns",    required_argument, 0, 'N'},
        {"proxy-host",   required_argument, 0, 'b'},
        {"proxy-port",   required_argument, 0, 'L'},
        {"mapper",       required_argument, 0, 'm'},
        {0, 0, 0, 0}
    };

    /* Set options to default values */
    options.num_conns    = NUM_CONNS;
    options.autocommit   = TRUE;
    options.backend.host = NULL;
    options.backend.port = 0;
    options.user         = NULL;
    options.pass         = NULL;
    options.db           = NULL;
    options.backend_file = NULL;
    options.phost        = NULL;
    options.pport        = PROXY_PORT;
    options.mapper       = NULL;

    /* Parse command-line options */
    while((c = getopt_long(argc, argv, "?h:P:D:u:p:f:N:aAb:L:m:", long_options, &opt)) != -1) {
        switch(c) {
            case '?':
                usage();
                return EXIT_SUCCESS;
            case 'h':
                options.backend.host = strdup(optarg);
                break;
            case 'P':
                options.backend.port = atoi(optarg);
                break;
            case 'D':
                options.db = strdup(optarg);
                break;
            case 'u':
                options.user = strdup(optarg);
                break;
            case 'p':
                options.pass = strdup(optarg);
                break;
            case 'f':
                options.backend_file = strdup(optarg);
                break;
            case 'N':
                options.num_conns = atoi(optarg);
                break;
            case 'a':
                options.autocommit = FALSE;
                break;
            case 'b':
                options.phost = strdup(optarg);
                break;
            case 'L':
                options.pport = atoi(optarg);
                break;
            case 'm':
                options.mapper = strdup(optarg);
                break;
            default:
                usage();
                return EX_USAGE;
        }

        opt = 0;
    }

    /* Set defaults for unspecified options */
    options.user = options.user ? options.user : strdup(BACKEND_USER);
    options.pass = options.pass ? options.pass : strdup(BACKEND_PASS);
    options.db   = options.db   ? options.db   : strdup(BACKEND_DB);

    if (options.backend_file) {
        if (options.backend.host || options.backend.port) {
            usage();
            return EX_USAGE;
        } else if (access(options.backend_file, R_OK)) {
            proxy_error("Error accessing backend file %s:%s", options.backend_file, errstr);
            return EX_NOINPUT;
        }
    } else {
        options.backend.host = options.backend.host ?
            options.backend.host : strdup(BACKEND_HOST);
        options.backend.port = options.backend.port ?
            options.backend.port : BACKEND_PORT;
    }
    
    return EXIT_SUCCESS;
}
