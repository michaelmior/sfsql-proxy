/******************************************************************************
 * check_backend.c
 *
 * Backend tests
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

#include "../proxy_backend.c"

#include <check.h>

/* Dummy threading functions */
void __wrap_proxy_threading_cancel(
    __attribute__((unused)) proxy_thread_t *threads,
    __attribute__((unused)) int num,
    __attribute__((unused)) pool_t *pool) {}
void __wrap_proxy_threading_cleanup(
    __attribute__((unused)) proxy_thread_t *threads,
    __attribute__((unused)) int num,
    __attribute__((unused)) pool_t *pool) {}

/* Error when trying to read backend with no filename */
START_TEST (test_backend_read_no_filename) {
    int num;

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(backend_read_file(NULL, &num) == NULL);
    fail_unless(num < 0);
} END_TEST

/* Error for reading empty backend file */
START_TEST (test_backend_read_not_exists) {
    int num;

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    fail_unless(backend_read_file(TESTS_DIR "backend/NOTHING.txt", &num) == NULL);
    fail_unless(num < 0);
} END_TEST

/* NULL produced when reading empty f ile */
START_TEST (test_backend_read_empty_file) {
    int num;
    proxy_backend_t **backends;

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    backends = backend_read_file(TESTS_DIR "backend/backends-empty.txt", &num);

    fail_unless(backends == NULL);
    fail_unless(num == 0);
} END_TEST

/* Correct parsing of backend file */
START_TEST (test_backend_read_file) {
    int num;
    proxy_backend_t **backends;

    backends = backend_read_file(TESTS_DIR "backend/backends.txt", &num);

    fail_unless(num == 2);

    fail_unless(strcmp(backends[0]->host, "127.0.0.1") == 0);
    fail_unless(backends[0]->port == 3306);
    fail_unless(strcmp(backends[1]->host, "127.0.0.1") == 0);
    fail_unless(backends[1]->port == 3307);

    free(backends[0]->host);
    free(backends[0]);
    free(backends[1]->host);
    free(backends[1]);
    free(backends);
} END_TEST

/* Error when reading more than maximum backends from file */
START_TEST (test_backend_read_too_many) {
    int num;
    proxy_backend_t **backends;

    FILE *null = fopen("/dev/null", "w");
    if (null) { fclose(stderr); stderr = null; }

    backends = backend_read_file(TESTS_DIR "backend/backends-too-many.txt", &num);

    fail_unless(backends == NULL);
    fail_unless(num > MAX_BACKENDS);
} END_TEST

/* LCG must produce all values */
START_TEST (test_backend_lcg) {
    int i, X = -1, N = 10;
    my_bool p[N];

    /* Mark all numbers as not picked */
    for (i=0; i < N; i++)
        p[i] = FALSE;

    /* Run through the LCG and update
     * values selected */
    for (i=0; i < N; i++) {
        X = lcg(X, N);
        fail_unless(!p[X]);
        p[X] = TRUE;
    }

    /* Ensure all values were selected */
    for (i=0; i < N; i++)
        fail_unless(p[i]);
} END_TEST

Suite *backend_suite(void) {
    Suite *s = suite_create("Backend");

    TCase *tc_file = tcase_create("File");
    tcase_add_test(tc_file, test_backend_read_no_filename);
    tcase_add_test(tc_file, test_backend_read_not_exists);
    tcase_add_test(tc_file, test_backend_read_empty_file);
    tcase_add_test(tc_file, test_backend_read_file);
    tcase_add_test(tc_file, test_backend_read_too_many);
    suite_add_tcase(s, tc_file);

    TCase *tc_lcg = tcase_create("LCG");
    tcase_add_test(tc_lcg, test_backend_lcg);
    suite_add_tcase(s, tc_lcg);

    return s;
}

int main(void) {
    int failed;
    Suite *s = backend_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
