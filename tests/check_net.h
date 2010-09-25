/* 
 * check_net.h
 *
 * Simple defines for network tests and stub code.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2010 by Michael Mior <mmior@cs.toronto.edu>
 *
 */

#ifndef _check_net_h
#define _check_net_h

#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_NAME  "mysql.sock"

typedef union {
    struct sockaddr_un sun;
    struct sockaddr    sa;
} sun_addr_t;

#endif /* _check_net_h */
