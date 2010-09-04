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
} options;

#endif /* _proxy_options_h */
