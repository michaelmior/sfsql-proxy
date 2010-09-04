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
} END_TEST

START_TEST (test_options_bad_file) {
    char *argv[] = { "./sfsql-proxy", "-fNOTHING.txt" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(parse_options(2, argv) == EX_NOINPUT);
} END_TEST

START_TEST (test_options_backend_and_file) {
    char *argv[] = { "./sfsql-proxy", "-fNOTHING.txt", "-h127.0.0.1" };

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }
    if (null) { fclose(stdout); stdout = null; }

    fail_unless(parse_options(3, argv) == EX_USAGE);
} END_TEST

Suite *options_suite(void) {
    Suite *s = suite_create("Options");

    TCase *tc_cli = tcase_create("Command-line parsing");
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
