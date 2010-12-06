/* 
 * proxy_options.h
 *
 * Proxy command-line options parsing.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#include <netinet/in.h>

#ifndef _proxy_options_h
#define _proxy_options_h

/** Default backend host */
#define BACKEND_HOST    "127.0.0.1"
/** Default backend port */
#define BACKEND_PORT    3306
/** Default backend user */
#define BACKEND_USER    "root"
/** Default backend password. */
#define BACKEND_PASS    "root"
/** Default backend database. */
#define BACKEND_DB      "test"
/** Default number of threads to pass backend queries. */
#define BACKEND_THREADS 10
/** Default number of connections per backend. */
#define NUM_CONNS       10

/** Default binding interface */
#define PROXY_IFACE     "eth0"
/** Default port to listen on for incoming connections. */
#define PROXY_PORT      4040
/** Default number of threads started to do client work. */
#define CLIENT_THREADS  10
/** Default seconds to wait before disconnecting client. */
#define CLIENT_TIMEOUT  5*60

void proxy_options_update_host();
int proxy_options_parse(int argc, char *argv[]);

/** Global struct holding program options. */
struct {
    /** TRUE if we need to daemonize */
    my_bool daemonize;
    /** TRUE if this proxy acts as coordinator. */
    my_bool coordinator;
    /** TRUE if this proxy is cloneable. */
    my_bool cloneable;

    /** Backend address info. */
    proxy_host_t backend;
    /** Port used to bypass secondary proxy server. */
    int bypass_port;
    /** UNIX socket filename */
    char *socket_file;

    /** Backend database */
    char *db;
    /** Backend username. */
    char *user;
    /** Backend password. */
    char *pass;
    /** File listing backends. */
    char *backend_file;
    /** Number of connections per backend. */
    int num_conns;
    /** Autocommit option for backends. */
    my_bool autocommit;
    /** Whether an identifier should be added. */
    my_bool add_ids;
    /** Whether or not to use two-phase commit. */
    my_bool two_pc;

    /** Host for proxy to bind to. */
    char phost[INET6_ADDRSTRLEN];
    /** Interface for proxy to bind to. */
    char *iface;
    /** Port for proxy to listen on. */
    int pport;
    /** Seconds to wait before disconnecting client. */
    int timeout;

    /** Name of the query mapper to use. */
    char *mapper;

    /** Number of client threads. */
    int client_threads;
    /** Number of backend threads. */
    int backend_threads;

    /** Enable verbose debugging. */
    my_bool verbose;
} options;

#endif /* _proxy_options_h */
