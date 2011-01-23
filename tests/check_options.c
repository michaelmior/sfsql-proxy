/******************************************************************************
 * check_options.c
 *
 * Test of command-line option processing
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

#include "../src/proxy_options.c"

#include <check.h>

#define TEST_STAT_FILE       "qps.out"
#define TEST_HOST            "127.0.0.2"
#define TEST_PORT            "3307"
#define TEST_BYPASS_PORT     "3306"
#define TEST_SOCKET          "/tmp/dummy.sock"
#define TEST_DB              "db"
#define TEST_USER            "test"
#define TEST_PASS            "test"
#define TEST_NUM_CONNS       "5"
#define TEST_PROXY_IFACE     "lo"
#define TEST_PROXY_HOST      "127.0.0.3"
#define TEST_PROXY_PORT      "3040"
#define TEST_ADMIN_PORT      "3041"
#define TEST_MAPPER          "dummy"
#define TEST_CLIENT_THREADS  "5"
#define TEST_CLIENT_TIMEOUT  "600"
#define TEST_BACKEND_THREADS "5"

/** @test Confirm that testing options are not the same as defaults */
START_TEST (test_options_test) {
    fail_unless(strcmp(TEST_HOST, BACKEND_HOST));
    fail_unless(atoi(TEST_PORT) != BACKEND_PORT);
    fail_unless(strcmp(TEST_SOCKET, MYSQL_UNIX_ADDR));
    fail_unless(strcmp(TEST_DB, BACKEND_DB));
    fail_unless(strcmp(TEST_USER, BACKEND_USER));
    fail_unless(strcmp(TEST_PASS, BACKEND_PASS));
    fail_unless(atoi(TEST_NUM_CONNS) != NUM_CONNS);
    fail_unless(TEST_PROXY_HOST != NULL);
    fail_unless(atoi(TEST_PROXY_PORT) != PROXY_PORT);
    fail_unless(atoi(TEST_ADMIN_PORT) != ADMIN_PORT);
    fail_unless(atoi(TEST_CLIENT_TIMEOUT) != CLIENT_TIMEOUT);
    fail_unless(TEST_MAPPER != NULL);
    fail_unless(atoi(TEST_CLIENT_THREADS) != CLIENT_THREADS);
    fail_unless(atoi(TEST_BACKEND_THREADS) != BACKEND_THREADS);
} END_TEST

/** @test Short option parsing */
START_TEST (test_options_short) {
    char *argv[] = { "./sfsql-proxy",
        "-v",
        "-d",
        "-C",
        "-c",
        "-q" TEST_STAT_FILE,
        "-A" TEST_ADMIN_PORT,
        "-h" TEST_HOST,
        "-P" TEST_PORT,
        "-y" TEST_BYPASS_PORT,
        "-n" TEST_CLIENT_TIMEOUT,
        "-D" TEST_DB,
        "-u" TEST_USER,
        "-p" TEST_PASS,
        "-i",
        "-2",
        "-a",
        "-b" TEST_PROXY_HOST,
        "-L" TEST_PROXY_PORT,
        "-m" TEST_MAPPER,
        "-t" TEST_CLIENT_THREADS };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(options.verbose);
    fail_unless(options.daemonize);
    fail_unless(options.coordinator);
    fail_unless(options.cloneable);
    fail_unless(strcmp(options.stat_file, TEST_STAT_FILE) == 0);
    fail_unless(options.admin_port == atoi(TEST_ADMIN_PORT));
    fail_unless(strcmp(options.backend.host, TEST_HOST) == 0);
    fail_unless(options.backend.port == atoi(TEST_PORT));
    fail_unless(options.bypass_port == atoi(TEST_BYPASS_PORT));
    fail_unless(strcmp(options.db, TEST_DB) == 0);
    fail_unless(strcmp(options.user, TEST_USER) == 0);
    fail_unless(strcmp(options.pass, TEST_PASS) == 0);
    fail_unless(options.add_ids);
    fail_unless(options.two_pc);
    fail_unless(!options.autocommit);
    fail_unless(strcmp(options.phost, TEST_PROXY_HOST) == 0);
    fail_unless(options.pport == atoi(TEST_PROXY_PORT));
    fail_unless(options.timeout == atoi(TEST_CLIENT_TIMEOUT));
    fail_unless(strcmp(options.mapper, TEST_MAPPER) == 0);
    fail_unless(options.client_threads == atoi(TEST_CLIENT_THREADS));
} END_TEST

/** @test Long option parsing */
START_TEST (test_options_long) {
    char *argv[] = { "./sfsql-proxy",
        "--verbose",
        "--daemonize",
        "--coordinator",
        "--cloneable",
        "--stat-file="       TEST_STAT_FILE,
        "--admin-port="      TEST_ADMIN_PORT,
        "--backend-host="    TEST_HOST,
        "--backend-port="    TEST_PORT,
        "--bypass-port="     TEST_BYPASS_PORT,
        "--backend-db="      TEST_DB,
        "--backend-user="    TEST_USER,
        "--backend-pass="    TEST_PASS,
        "--add-ids",
        "--two-pc",
        "--proxy-host="      TEST_PROXY_HOST,
        "--proxy-port="      TEST_PROXY_PORT,
        "--timeout="         TEST_CLIENT_TIMEOUT,
        "--mapper="          TEST_MAPPER,
        "--client-threads="  TEST_CLIENT_THREADS };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(options.verbose);
    fail_unless(options.daemonize);
    fail_unless(options.coordinator);
    fail_unless(options.cloneable);
    fail_unless(strcmp(options.stat_file, TEST_STAT_FILE) == 0);
    fail_unless(options.admin_port == atoi(TEST_ADMIN_PORT));
    fail_unless(strcmp(options.backend.host, TEST_HOST) == 0);
    fail_unless(options.backend.port == atoi(TEST_PORT));
    fail_unless(options.bypass_port == atoi(TEST_BYPASS_PORT));
    fail_unless(strcmp(options.db, TEST_DB) == 0);
    fail_unless(strcmp(options.user, TEST_USER) == 0);
    fail_unless(strcmp(options.pass, TEST_PASS) == 0);
    fail_unless(options.add_ids);
    fail_unless(options.two_pc);
    fail_unless(strcmp(options.phost, TEST_PROXY_HOST) == 0);
    fail_unless(options.pport == atoi(TEST_PROXY_PORT));
    fail_unless(options.timeout == atoi(TEST_CLIENT_TIMEOUT));
    fail_unless(strcmp(options.mapper, TEST_MAPPER) == 0);
    fail_unless(options.client_threads == atoi(TEST_CLIENT_THREADS));
} END_TEST

/** @test Assignment of default options */
START_TEST (test_options_defaults) {
    /* Specify no arguments */
    fail_unless(proxy_options_parse(0, NULL) == EXIT_SUCCESS);

    /* Check that all options have their correct values */
    fail_unless(!options.verbose);
    fail_unless(!options.daemonize);
    fail_unless(!options.coordinator);
    fail_unless(!options.cloneable);
    fail_unless(options.admin_port == ADMIN_PORT);
    fail_unless(!options.add_ids);
    fail_unless(!options.two_pc);
    fail_unless(options.autocommit);
    fail_unless(strcmp(options.backend.host, BACKEND_HOST) == 0);
    fail_unless(options.bypass_port < 0);
    fail_unless(options.socket_file == NULL);
    fail_unless(options.backend.port == BACKEND_PORT);
    fail_unless(strcmp(options.user, BACKEND_USER) == 0);
    fail_unless(strcmp(options.pass, BACKEND_PASS) == 0);
    fail_unless(strcmp(options.db, BACKEND_DB) == 0);
    fail_unless(options.backend_file == NULL);
    fail_unless(options.num_conns = NUM_CONNS);
    fail_unless(options.pport == PROXY_PORT);
    fail_unless(options.timeout == CLIENT_TIMEOUT);
    fail_unless(options.mapper == NULL);
    fail_unless(options.client_threads == CLIENT_THREADS);
} END_TEST

/** @test Specification of invalid file */
START_TEST (test_options_bad_file) {
    char *argv[] = { "./sfsql-proxy", "-fNOTHING.txt" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EX_NOINPUT);
} END_TEST

/** @test Specification of both backend and filename */
START_TEST (test_options_backend_and_file) {
    char *argv[] = { "./sfsql-proxy", "-f" TESTS_DIR "backend/backends.txt", "-h" BACKEND_HOST };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }
    if (null) { fclose(stdout); stdout = null; }

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EX_USAGE);
} END_TEST

/** @test Specification of both file and socket */
START_TEST (test_options_file_and_socket) {
    char *argv[] = { "./sfsql-proxy", "-f" TESTS_DIR "backend/backends.txt", "-s" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }
    if (null) { fclose(stdout); stdout = null; }

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EX_USAGE);
} END_TEST

/** @test Specification of both backend and socket */
START_TEST (test_options_backend_and_socket) {
    char *argv[] = { "./sfsql-proxy", "-h" BACKEND_HOST, "-s" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }
    if (null) { fclose(stdout); stdout = null; }

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EX_USAGE);
} END_TEST

/** @test Default socket path assigned if none specified */
START_TEST (test_options_socket_default) {
    char *argv[] = { "./sfsql-proxy", "-s" };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(strcmp(options.socket_file, MYSQL_UNIX_ADDR) == 0);
} END_TEST

/** @test Options which should not be specfied with no backend file */
START_TEST (test_options_no_file) {
    char *argv1[] = { "./sfsql-proxy",
        "-N" TEST_NUM_CONNS };

    extern int optind;
    char *argv2[] = { "./sfsql-proxy",
        "-T" TEST_BACKEND_THREADS };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(proxy_options_parse(sizeof(argv1)/sizeof(*argv1), argv1) == EX_USAGE);

    optind = 0;
    fail_unless(proxy_options_parse(sizeof(argv2)/sizeof(*argv2), argv2) == EX_USAGE);
} END_TEST;

/** @test Backend threads and number of connections can be specified for one backend
 *        if we are the coordinator */
START_TEST (test_options_coordinator) {
    char *argv[] = { "./sfsql-proxy",
        "-C",
        "-N" TEST_NUM_CONNS,
        "-T" TEST_BACKEND_THREADS };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);
} END_TEST;

/** @test Short options only valid with file specified */
START_TEST (test_options_file_short) {
    char *argv[] = { "./sfsql-proxy",
        "-f" TESTS_DIR "backend/backends.txt",
        "-N" TEST_NUM_CONNS,
        "-T" TEST_BACKEND_THREADS };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(options.num_conns = atoi(TEST_NUM_CONNS));
    fail_unless(options.backend_threads = atoi(TEST_BACKEND_THREADS));
} END_TEST

/** @test Long options only valid with file specified */
START_TEST (test_options_file_long) {
    char *argv[] = { "./sfsql-proxy",
        "-f" TESTS_DIR "backend/backends.txt",
        "--num-conns="       TEST_NUM_CONNS,
        "--backend-threads=" TEST_BACKEND_THREADS };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(options.num_conns = atoi(TEST_NUM_CONNS));
    fail_unless(options.backend_threads = atoi(TEST_BACKEND_THREADS));
} END_TEST

/** @test Default parameters only valid when file specified */
START_TEST (test_options_file_default) {
    char *argv[] = { "./sfsql-proxy", "-f" TESTS_DIR "backend/backends.txt" };

    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(options.num_conns == NUM_CONNS);
    fail_unless(options.backend_threads = BACKEND_THREADS);
} END_TEST

/** @test Interface options parsing */
START_TEST (test_options_iface) {
    char *argv1[] = { "./sfsql-proxy",
        "-I" TEST_PROXY_IFACE };

    extern int optind;
    char *argv2[] = { "./sfsql-proxy",
        "--interface=" TEST_PROXY_IFACE };

    fail_unless(proxy_options_parse(sizeof(argv1)/sizeof(*argv1), argv1) == EXIT_SUCCESS);
    fail_unless(options.phost[0] != '\0');

    optind = 0;
    fail_unless(proxy_options_parse(sizeof(argv2)/sizeof(*argv2), argv2) == EXIT_SUCCESS);
} END_TEST

/** @test Specification of 'any' interface */
START_TEST (test_options_iface_any) {
    char *argv[] = { "./sfsql-proxy",
        "-Iany" };
    fail_unless(proxy_options_parse(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);
    fail_unless(options.phost[0] == '\0');
} END_TEST

Suite *options_suite(void) {
    Suite *s = suite_create("Options");

    TCase *tc_cli = tcase_create("Command-line parsing");
    tcase_add_test(tc_cli, test_options_test);
    tcase_add_test(tc_cli, test_options_short);
    tcase_add_test(tc_cli, test_options_long);
    tcase_add_test(tc_cli, test_options_defaults);
    suite_add_tcase(s, tc_cli);

    TCase *tc_file = tcase_create("File and socket parsing");
    tcase_add_test(tc_file, test_options_bad_file);
    tcase_add_test(tc_file, test_options_backend_and_file);
    tcase_add_test(tc_file, test_options_file_and_socket);
    tcase_add_test(tc_file, test_options_backend_and_socket);
    tcase_add_test(tc_file, test_options_socket_default);
    tcase_add_test(tc_file, test_options_no_file);
    tcase_add_test(tc_file, test_options_coordinator);
    tcase_add_test(tc_file, test_options_file_short);
    tcase_add_test(tc_file, test_options_file_long);
    tcase_add_test(tc_file, test_options_file_default);
    suite_add_tcase(s, tc_file);

    TCase *tc_iface = tcase_create("Interface option parsing");
    tcase_add_test(tc_iface, test_options_iface);
    tcase_add_test(tc_iface, test_options_iface_any);
    suite_add_tcase(s, tc_iface);

    return s;
}

int main(void) {
    int failed;
    Suite *s = options_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
