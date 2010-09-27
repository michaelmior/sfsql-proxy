/* 
 * proxy_options.h
 *
 * Includes for command-line parsing
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

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

#define PROXY_PORT      4040        /** Default port to listen on for incoming connections. */
#define CLIENT_THREADS  10          /** Default number of threads started to do client work. */
#define CLIENT_TIMEOUT  5*60        /** Default seconds to wait before disconnecting client. */

int parse_options(int argc, char *argv[]);

/** Global struct holding program options. */
struct {
    /** TRUE if the user asked for help */
    my_bool help;

    /** Backend address info. */
    proxy_backend_t backend;
    /** Seconds to wait before disconnecting client. */
    int timeout;
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
    /** Host for proxy to bind to. */
    char *phost;
    /** Port for proxy to listen on. */
    int pport;
    /** Name of the query mapper to use. */
    char *mapper;
    /** Numberof client threads. */
    int client_threads;
    /** Numbr of backend threads. */
    int backend_threads;
} options;

#endif /* _proxy_options_h */