/******************************************************************************
 * proxy_options.c
 *
 * Proxy command-line options parsing.
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>

#include "proxy.h"

/**
 * Print a simple usage message with command-line arguments.
 **/
static void usage() {
    printf(
            "SnowFlock SQL proxy server - (C) Michael Mior <mmior@cs.toronto.edu>, 2010\n\n"

            "Options:\n"
            "\t--help,             -?\tShow this message\n"
            "\t--daemonize,        -d\tDaemonize\n"
            "\t--coordinator,      -C\tProxy should act as coordinator\n"
            "\t--cloneable,        -c\tProxy should execute cloning when signalled\n\n"

            "Backend options:\n"
            "\t--backend-host,    -h\tHost to forward queries to (default: 127.0.0.1)\n"
            "\t--backend-port,    -P\tPort of the backend host (default: 3306)\n"
            "\t--bypass-port,     -y\tPort used to bypass secondary proxy for read-only queries\n"
            "\t--socket,          -s\tUse a UNIX socket for the backend connection\n\n"

            "\t--backend-db,      -D\tName of database on the backend (default: test)\n"
            "\t--backend-user,    -u\tUser for backend server (default: root)\n"
            "\t--backend-pass,    -p\tPassword for backend user\n\n"
            "\t--backend-file,    -f\tFile listing available backends\n"
            "\t                     \t(cannot be specified with above options)\n\n"
            "\t--num-conns,       -N\tNumber connections per backend\n"
            "\t                   -a\tDisable autocommit (default is enabled)\n"
            "\t--add-ids,         -i\tTag transactions with unique identifiers\n"
            "\t--two-pc,          -2\tUse two-phase commit to ensure consistency across backends\n\n"

            "Proxy options:\n"
            "\t--proxy-host,      -b\tBinding address (default is 0.0.0.0)\n"
            "\t--interface,       -I\tInterface to bind to, or 'any' for all interfaces (default is eth0)\n"
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
 * Set default values for options in the ::options structure.
 **/
static void set_option_defaults() {
    /* Set options to default values */
    options.daemonize       = FALSE;
    options.coordinator     = FALSE;
    options.cloneable       = FALSE;

    options.num_conns       = -1;
    options.add_ids         = FALSE;
    options.two_pc          = FALSE;
    options.autocommit      = TRUE;
    options.backend.host    = NULL;
    options.backend.port    = 0;
    options.bypass_port     = -1;
    options.unix_socket     = FALSE;
    options.socket_file     = NULL;
    options.user            = NULL;
    options.pass            = NULL;
    options.db              = NULL;
    options.backend_file    = NULL;
    options.phost[0]        = '\0';
    options.iface           = NULL;
    options.pport           = PROXY_PORT;
    options.timeout         = CLIENT_TIMEOUT;
    options.mapper          = NULL;
    options.client_threads  = CLIENT_THREADS;
    options.backend_threads = -1;
}

/**
 * Parse command-line options
 *
 * @param argc Number of arguments.
 * @param argv Argument list.
 **/
int proxy_options_parse(int argc, char *argv[]) {
    int c, opt=0;
    static struct option long_options[] = {
        {"help",            no_argument,       0, '?'},
        {"daemonize",       no_argument,       0, 'd'},
        {"coordinator",     no_argument,       0, 'C'},
        {"cloneable",       no_argument,       0, 'c'},
        {"backend-host",    required_argument, 0, 'h'},
        {"backend-port",    required_argument, 0, 'P'},
        {"bypass-port",     required_argument, 0, 'y'},
        {"socket",          optional_argument, 0, 's'},
        {"backend-db",      required_argument, 0, 'D'},
        {"backend-user",    required_argument, 0, 'u'},
        {"backend-pass",    required_argument, 0, 'p'},
        {"backend-file",    required_argument, 0, 'f'},
        {"num-conns",       required_argument, 0, 'N'},
        {"add-ids",         no_argument,       0, 'i'},
        {"two-pc",          no_argument,       0, '2'},
        {"proxy-host",      required_argument, 0, 'b'},
        {"interface" ,      required_argument, 0, 'I'},
        {"proxy-port",      required_argument, 0, 'L'},
        {"timeout",         required_argument, 0, 'n'},
        {"mapper",          required_argument, 0, 'm'},
        {"client-threads",  required_argument, 0, 't'},
        {"backend-threads", required_argument, 0, 'T'},
        {0, 0, 0, 0}
    };

    set_option_defaults();

    /* Parse command-line options */
    while((c = getopt_long(argc, argv, "?dCch:P:y:s::n:D:u:p:f:N:i2aAb:I:L:m:t:T:", long_options, &opt)) != -1) {
        switch(c) {
            case '?':
                usage();
                return EX_USAGE;
            case 'd':
                options.daemonize = TRUE;
                break;
            case 'C':
                options.coordinator = TRUE;
                break;
            case 'c':
                options.cloneable = TRUE;
                break;
            case 'h':
                options.backend.host = optarg;
                break;
            case 'P':
                options.backend.port = atoi(optarg);
                break;
            case 'y':
                options.bypass_port = atoi(optarg);
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
            case '2':
                options.two_pc = TRUE;
                break;
            case 'a':
                options.autocommit = FALSE;
                break;
            case 'b':
                strncpy(options.phost, optarg, INET6_ADDRSTRLEN);
                break;
            case 'I':
                options.iface = optarg;
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

    /* Can't specify both a binding interface and address */
    if (options.iface && options.phost[0]) {
        usage();
        return EX_USAGE;
    }

    if (!options.iface)
        options.iface = PROXY_IFACE;

    /* Get the IP address of the interface */
    if (options.phost[0] == '\0' && strcasecmp("any", options.iface)) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct ifreq ifr;
        union {
            struct sockaddr *sa;
            struct sockaddr_in *sin;
        } addr;
        char *phost;

        ifr.ifr_addr.sa_family = AF_UNSPEC;
        strncpy(ifr.ifr_name, options.iface, IFNAMSIZ-1);

        ioctl(fd, SIOCGIFADDR, &ifr);
        close(fd);

        /* Convert to string and save in the binding address */
        addr.sa = &ifr.ifr_addr;
        phost = inet_ntoa(addr.sin->sin_addr);
        strncpy(options.phost, phost, INET6_ADDRSTRLEN);
    }

    /* Set defaults for unspecified options */
    options.user = options.user ?: BACKEND_USER;
    options.pass = options.pass ?: BACKEND_PASS;
    options.db   = options.db   ?: BACKEND_DB;

    /* If a file was specified, make sure no other host options were used */
    if (options.backend_file) {
        if (options.backend.host || options.backend.port || options.unix_socket) {
            usage();
            return EX_USAGE;
        } else if (access(options.backend_file, R_OK)) {
            fprintf(stderr, "Error accessing backend file %s:%s\n", options.backend_file, strerror(errno));
            return EX_NOINPUT;
        }

        options.backend_threads = BACKEND_THREADS;
        options.num_conns = NUM_CONNS;
    } else {
        if (options.backend_threads > 0 || options.num_conns > 0) {
            fprintf(stderr, "Can't specify backend threads or connections with only one backend\n");
            return EX_USAGE;
        }

        if ((options.backend.host || options.backend.port || options.coordinator) && options.unix_socket) {
            usage();
            return EX_USAGE;
        }

        options.backend.host = options.backend.host ?: BACKEND_HOST;
        options.backend.port = options.backend.port ?: BACKEND_PORT;

        if (options.coordinator)
            options.backend_threads = BACKEND_THREADS;
        options.num_conns = options.client_threads;
    }
    
    return EXIT_SUCCESS;
}
