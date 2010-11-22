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

#include "../src/proxy_backend.c"

#include <check.h>
#include <signal.h>

/* Externs */
ulong transaction_id;
struct hashtable *trans_table;
volatile sig_atomic_t server_id;

/* Dummy threading functions */
void __wrap_proxy_threading_cancel(
    __attribute__((unused)) proxy_thread_t *threads,
    __attribute__((unused)) int num,
    __attribute__((unused)) pool_t *pool) {}
void __wrap_proxy_threading_cleanup(
    __attribute__((unused)) proxy_thread_t *threads,
    __attribute__((unused)) int num,
    __attribute__((unused)) pool_t *pool) {}
void __wrap_proxy_threading_mask() {}

/* Dummy network functions */
my_bool __wrap_proxy_net_send_ok(
        __attribute__((unused)) MYSQL *mysql,
        __attribute__((unused)) uint warnings,
        __attribute__((unused)) ulong affected_rows,
        __attribute__((unused)) ulonglong last_insert_id) { return FALSE; }
my_bool __wrap_proxy_net_send_error(
        __attribute__((unused))MYSQL *mysql,
        __attribute__((unused))int sql_errno,
        __attribute__((unused))const char *err) { return FALSE; }

/* Dummy hash functions */
int hashtable_insert(
    __attribute__((unused)) struct hashtable *h,
    __attribute__((unused)) void *k,
    __attribute__((unused)) void *v) { return 0; }
void* hashtable_remove(
    __attribute__((unused)) struct hashtable *h,
    __attribute__((unused)) void *k) { return NULL; }

void proxy_clone_notify() {}

volatile sig_atomic_t cloning = 0;

/** @test Error when trying to read backend with no filename */
START_TEST (test_backend_read_no_filename) {
    int num;

    fail_unless(backend_read_file(NULL, &num) == NULL);
    fail_unless(num < 0);
} END_TEST

/** @test Error for reading empty backend file */
START_TEST (test_backend_read_not_exists) {
    int num;

    fail_unless(backend_read_file(TESTS_DIR "backend/NOTHING.txt", &num) == NULL);
    fail_unless(num < 0);
} END_TEST

/** @test NULL produced when reading empty file */
START_TEST (test_backend_read_empty_file) {
    int num;
    proxy_host_t **backends;

    backends = backend_read_file(TESTS_DIR "backend/backends-empty.txt", &num);

    fail_unless(backends == NULL);
    fail_unless(num == 0);
} END_TEST

/** @test Correct parsing of backend file */
START_TEST (test_backend_read_file) {
    int num;
    proxy_host_t **backends;

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

/** @test Correct parsing of backend file with a missing port number*/
START_TEST (test_backend_read_file_noport) {
    int num;
    proxy_host_t **backends;

    backends = backend_read_file(TESTS_DIR "backend/backends-noport.txt", &num);

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

Suite *backend_suite(void) {
    Suite *s = suite_create("Backend");

    TCase *tc_file = tcase_create("File");
    tcase_add_test(tc_file, test_backend_read_no_filename);
    tcase_add_test(tc_file, test_backend_read_not_exists);
    tcase_add_test(tc_file, test_backend_read_empty_file);
    tcase_add_test(tc_file, test_backend_read_file);
    tcase_add_test(tc_file, test_backend_read_file_noport);
    suite_add_tcase(s, tc_file);

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
