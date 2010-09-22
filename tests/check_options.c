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

#include "../proxy_options.c"

#include <check.h>

#define TEST_HOST            "127.0.0.2"
#define TEST_PORT            "3307"
#define TEST_DB              "db"
#define TEST_USER            "test"
#define TEST_PASS            "test"
#define TEST_NUM_CONNS       "5"
#define TEST_PROXY_HOST      "127.0.0.3"
#define TEST_PROXY_PORT      "4041"
#define TEST_MAPPER          "dummy"
#define TEST_CLIENT_THREADS  "5"
#define TEST_CLIENT_TIMEOUT  "600"
#define TEST_BACKEND_THREADS "5"

/* Confirm that testing options are not the same as defaults */
START_TEST (test_options_test) {
    fail_unless(strcmp(TEST_HOST, BACKEND_HOST));
    fail_unless(atoi(TEST_PORT) != BACKEND_PORT);
    fail_unless(strcmp(TEST_DB, BACKEND_DB));
    fail_unless(strcmp(TEST_USER, BACKEND_USER));
    fail_unless(strcmp(TEST_PASS, BACKEND_PASS));
    fail_unless(atoi(TEST_NUM_CONNS) != NUM_CONNS);
    fail_unless(TEST_PROXY_HOST != NULL);
    fail_unless(atoi(TEST_PROXY_PORT) != PROXY_PORT);
    fail_unless(atoi(TEST_CLIENT_TIMEOUT) != CLIENT_TIMEOUT);
    fail_unless(TEST_MAPPER != NULL);
    fail_unless(atoi(TEST_CLIENT_THREADS) != CLIENT_THREADS);
    fail_unless(atoi(TEST_BACKEND_THREADS) != BACKEND_THREADS);
} END_TEST

/* Short option parsing */
START_TEST (test_options_short) {
    char *argv[] = { "./sfsql-proxy",
        "-h" TEST_HOST,
        "-P" TEST_PORT,
        "-n" TEST_CLIENT_TIMEOUT,
        "-D" TEST_DB,
        "-u" TEST_USER,
        "-p" TEST_PASS,
        "-N" TEST_NUM_CONNS,
        "-a",
        "-b" TEST_PROXY_HOST,
        "-L" TEST_PROXY_PORT,
        "-m" TEST_MAPPER,
        "-t" TEST_CLIENT_THREADS,
        "-T" TEST_BACKEND_THREADS };

    fail_unless(parse_options(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(strcmp(options.backend.host, TEST_HOST) == 0);
    fail_unless(options.backend.port == atoi(TEST_PORT));
    fail_unless(strcmp(options.db, TEST_DB) == 0);
    fail_unless(strcmp(options.user, TEST_USER) == 0);
    fail_unless(strcmp(options.pass, TEST_PASS) == 0);
    fail_unless(options.num_conns == atoi(TEST_NUM_CONNS));
    fail_unless(!options.autocommit);
    fail_unless(strcmp(options.phost, TEST_PROXY_HOST) == 0);
    fail_unless(options.pport == atoi(TEST_PROXY_PORT));
    fail_unless(options.timeout == atoi(TEST_CLIENT_TIMEOUT));
    fail_unless(strcmp(options.mapper, TEST_MAPPER) == 0);
    fail_unless(options.client_threads == atoi(TEST_CLIENT_THREADS));
    fail_unless(options.backend_threads == atoi(TEST_BACKEND_THREADS));
} END_TEST

/* Long option parsing */
START_TEST (test_options_long) {
    char *argv[] = { "./sfsql-proxy",
        "--backend-host="    TEST_HOST,
        "--backend-port="    TEST_PORT,
        "--backend-db="      TEST_DB,
        "--backend-user="    TEST_USER,
        "--backend-pass="    TEST_PASS,
        "--num-conns="       TEST_NUM_CONNS,
        "--proxy-host="      TEST_PROXY_HOST,
        "--proxy-port="      TEST_PROXY_PORT,
        "--timeout="         TEST_CLIENT_TIMEOUT,
        "--mapper="          TEST_MAPPER,
        "--client-threads="  TEST_CLIENT_THREADS,
        "--backend-threads=" TEST_BACKEND_THREADS };

    fail_unless(parse_options(sizeof(argv)/sizeof(*argv), argv) == EXIT_SUCCESS);

    fail_unless(strcmp(options.backend.host, TEST_HOST) == 0);
    fail_unless(options.backend.port == atoi(TEST_PORT));
    fail_unless(strcmp(options.db, TEST_DB) == 0);
    fail_unless(strcmp(options.user, TEST_USER) == 0);
    fail_unless(strcmp(options.pass, TEST_PASS) == 0);
    fail_unless(options.num_conns == atoi(TEST_NUM_CONNS));
    fail_unless(strcmp(options.phost, TEST_PROXY_HOST) == 0);
    fail_unless(options.pport == atoi(TEST_PROXY_PORT));
    fail_unless(options.timeout == atoi(TEST_CLIENT_TIMEOUT));
    fail_unless(strcmp(options.mapper, TEST_MAPPER) == 0);
    fail_unless(options.client_threads == atoi(TEST_CLIENT_THREADS));
    fail_unless(options.backend_threads == atoi(TEST_BACKEND_THREADS));
} END_TEST

/* Assignment of default options */
START_TEST (test_options_defaults) {
    /* Specify no arguments */
    fail_unless(parse_options(0, NULL) == EXIT_SUCCESS);

    /* Check that all options have their correct values */
    fail_unless(options.num_conns == NUM_CONNS);
    fail_unless(options.autocommit);
    fail_unless(strcmp(options.backend.host, BACKEND_HOST) == 0);
    fail_unless(options.backend.port == BACKEND_PORT);
    fail_unless(strcmp(options.user, BACKEND_USER) == 0);
    fail_unless(strcmp(options.pass, BACKEND_PASS) == 0);
    fail_unless(strcmp(options.db, BACKEND_DB) == 0);
    fail_unless(options.backend_file == NULL);
    fail_unless(options.phost == NULL);
    fail_unless(options.pport == PROXY_PORT);
    fail_unless(options.timeout == CLIENT_TIMEOUT);
    fail_unless(options.mapper == NULL);
    fail_unless(options.client_threads == CLIENT_THREADS);
    fail_unless(options.backend_threads == BACKEND_THREADS);
} END_TEST

/* Specification of invalid file */
START_TEST (test_options_bad_file) {
    char *argv[] = { "./sfsql-proxy", "-fNOTHING.txt" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(parse_options(sizeof(argv)/sizeof(*argv), argv) == EX_NOINPUT);
} END_TEST

/* Specification of both backend and filename */
START_TEST (test_options_backend_and_file) {
    char *argv[] = { "./sfsql-proxy", "-fNOTHING.txt", "-h" BACKEND_HOST };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }
    if (null) { fclose(stdout); stdout = null; }

    fail_unless(parse_options(sizeof(argv)/sizeof(*argv), argv) == EX_USAGE);
} END_TEST

Suite *options_suite(void) {
    Suite *s = suite_create("Options");

    TCase *tc_cli = tcase_create("Command-line parsing");
    tcase_add_test(tc_cli, test_options_test);
    tcase_add_test(tc_cli, test_options_short);
    tcase_add_test(tc_cli, test_options_long);
    tcase_add_test(tc_cli, test_options_defaults);
    tcase_add_test(tc_cli, test_options_bad_file);
    tcase_add_test(tc_cli, test_options_backend_and_file);
    suite_add_tcase(s, tc_cli);

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
