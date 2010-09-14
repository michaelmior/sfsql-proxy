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

#define BACKEND_HOST    "127.0.0.1" /** Default backend host */
#define BACKEND_PORT    3306        /** Default backend port */
#define BACKEND_USER    "root"      /** Default backend user */
#define BACKEND_PASS    "root"      /** Default backend password */
#define BACKEND_DB      "test"      /** Default backend database */
#define BACKEND_THREADS 10          /** Default number of threads to pass backend queries */
#define NUM_CONNS       10          /** Default number of connections per backend */

#define PROXY_PORT      4040        /** Default port to listen on for incoming connections */
#define CLIENT_THREADS   10          /** Default number of threads started to do client work */

int parse_options(int argc, char *argv[]);

struct {
    proxy_backend_t backend; /** Backend address info */
    char *db;                /** Backend database */
    char *user;              /** Backend username */
    char *pass;              /** Backend password */
    char *backend_file;      /** File listing backends */
    int num_conns;           /** Number of connections per backend */
    my_bool autocommit;      /** Autocommit option for backends */
    char *phost;             /** Host for proxy to bind to */
    int pport;               /** Port for proxy to listen on */
    char *mapper;            /** Name of the query mapper to use */
    int client_threads;
    int backend_threads;
} options;

#endif /* _proxy_options_h */
