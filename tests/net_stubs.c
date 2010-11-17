/******************************************************************************
 * net_stubs.c
 *
 * Wrappers for setting up dummy client in network tests
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

#include <my_global.h>
#include <mysql_com.h>
#include <mysql.h>

#include <sys/types.h>

#include "check_net.h"
#include "proxy.h"

MYSQL* client_init(Vio *vio);
my_bool __real_my_net_init(NET *net, Vio *vio);
void __real_randominit(struct rand_struct *rand_st, ulong seed1, ulong seed2);

my_bool __wrap_my_net_init(NET *net, Vio *vio) {
    sun_addr_t addr;
    int s;

    /* Connect to the local socket */
    addr.sun.sun_family = AF_UNIX;
    strncpy(addr.sun.sun_path, SOCK_NAME, sizeof(struct sockaddr_un) - sizeof(short));

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0 || connect(s, &(addr.sa), sizeof(struct sockaddr_un)))
        return TRUE;

    /* Finish structure setup */
    vio = vio_new(s, VIO_TYPE_SOCKET, 0);
    vio->localhost = TRUE;
    __real_my_net_init(net, vio);

    return FALSE;
}

my_bool __wrap_proxy_backend_query(__attribute__((unused)) MYSQL *proxy, const char *query, __attribute__((unused)) ulong length) {
    /* For now, this function does nothing.
     * In the future, we can check that
     * client queries are properly received. */
    printf("Received query: %s\n", query);
    return FALSE;
}

/* Don't need to touch the pool here */
void __wrap_proxy_pool_return(__attribute__((unused)) pool_t *pool, __attribute__((unused)) int idx) {}

/* No need for thread masking in tests */
void __wrap_proxy_threading_mask() {}

/* Use a fixed seed for tests */
void __wrap_randominit(struct rand_struct *rand_st, __attribute__((unused)) ulong seed1, __attribute__((unused)) ulong seed2) {
    __real_randominit(rand_st, 0, 0);
}

void __wrap_proxy_do_clone(__attribute__((unused)) int nclones, __attribute__((unused)) char *err, __attribute__((unused)) int errlen) {}
void __wrap_list_tickets(void) {}
void __wrap_read_ticket_info(__attribute__((unused)) char *ticket) {}
