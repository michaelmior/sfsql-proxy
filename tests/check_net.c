/******************************************************************************
 * check_net.c
 *
 * Networking tests
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

#include <check.h>
#include "check_net.h"
#include "../proxy.h"

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

MYSQL* client_init(Vio *vio);

int send_file(char *filename, int sd) {
    FILE *fp;
    ulong size;
    int n;
    char *buf;

    fp = fopen(filename, "r");
    if (!fp)
        return -1;

    /* Get packet size */
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    buf = (char*) malloc(size);

    /* Read the packet */
    while (size > 0) {
        n = fread(buf, 1, size, fp);
        if (write(sd, buf, n) != n)
            return -1;
        size -= n;
        buf += n;
    }

    return 0;
}

int compare_to_file(char *filename, int sd) {
    FILE *fp;
    ulong size, c, i, n;
    char *buf1, *buf2, *pos1, *pos2;

    fp = fopen(filename, "r");

    /* Get packet size */
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    buf1 = (char*) malloc(size);
    buf2 = (char*) malloc(size);

    /* Read the packet */
    c = size;
    pos1 = buf1;
    pos2 = buf2;
    while (c > 0) {
        n = fread(pos1, 1, size, fp);
        if (read(sd, pos2, n) != (int)n)
            return -1;
        c -= n;
        pos1 += n;
        pos2 += n;
    }

    /* Compare the two buffers */
    for (i=0; i<size; i++)
        if (buf1[i] != buf2[i])
            return -1;

    return 0;
}

START_TEST (test_net_handshake) {
    struct sockaddr_un sa;
    struct sockaddr_in addr;
    int s, ns, pid;
    socklen_t i;
    MYSQL *mysql;

    signal(SIGPIPE, SIG_IGN);
    remove(SOCK_NAME);

    /* Set up client connection */
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCK_NAME, sizeof(struct sockaddr_un) - sizeof(short));

    fail_unless((s = socket(AF_UNIX, SOCK_STREAM, 0)) > 0);
    fail_unless(bind(s, (struct sockaddr*) &sa, sizeof(struct sockaddr_un)) == 0);
    fail_unless(listen(s, 0) == 0);

    /* Fork to handle both sides of the connection */
    switch (pid = fork()) {
        case 0:
            /* Set up a dummy address */
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = 0;

            /* Try to handshake */
            mysql = client_init((Vio*) NULL);
            proxy_net_handshake(mysql, &addr, 0);
            exit(0);
        default:
            i = sizeof(struct sockaddr_un);
            fail_unless((ns = accept(s, (struct sockaddr*) &sa, &i)) > 0);

            fail_unless(compare_to_file(TESTS_DIR "net/handshake-greeting.cap", ns) == 0);
            fail_unless(send_file(TESTS_DIR "net/handshake-auth.cap", ns) == 0);
            fail_unless(compare_to_file(TESTS_DIR "net/handshake-ok.cap", ns) == 0);

            close(s);
            close(ns);
            remove(SOCK_NAME);
    }
} END_TEST

Suite *net_suite(void) {
    Suite *s = suite_create("Net");

    TCase *tc_auth = tcase_create("Authentication");
    tcase_add_test(tc_auth, test_net_handshake);
    suite_add_tcase(s, tc_auth);

    return s;
}

int main(void) {
    int failed;
    Suite *s = net_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
