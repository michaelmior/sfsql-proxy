/******************************************************************************
 * check_pool.c
 *
 * Pool tests
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

#include "../src/proxy_pool.c"

#include <check.h>

static pool_t *pool;

/** Fixture to create a pool used in tests. */
void setup () {
    pool = proxy_pool_new(1);
}

/** Fixture to destroy a pool used in tests. */
void teardown() {
    proxy_pool_destroy(pool);
}

/** @test New pool is correctly allocated */
START_TEST (test_pool_new) {
    fail_unless(pool != NULL);
    fail_unless(pool->__alloc >= 1);
    fail_unless(pool->size == 1);
    fail_unless(pool->avail[0] == TRUE);
} END_TEST

/** @test Passing NULL when destroying pool does nothing */
START_TEST (test_pool_destroy_null) {
    proxy_pool_destroy(NULL);
} END_TEST

/** @test Trying to create an empty pool returns NULL */
START_TEST (test_pool_new_empty) {
    pool_t *pool;

    pool = proxy_pool_new(0);
    fail_unless(pool == NULL);
} END_TEST

/** @test Pool can be successfully grown */
START_TEST (test_pool_grow) {
    int i;

    proxy_pool_set_size(pool, 10);

    fail_unless(pool->size == 10);
    fail_unless(pool->__alloc >= 10);

    for (i=1; i<10; i++)
        fail_unless(pool->avail[i] == TRUE);
} END_TEST

/** @test Pool can be successfully shrunk */
START_TEST (test_pool_shrink) {
    pool_t *pool;

    pool = proxy_pool_new(10);
    proxy_pool_set_size(pool, 1);

    fail_unless(pool->size == 1);
    fail_unless(pool->__alloc >= 1);

    proxy_pool_destroy(pool);
} END_TEST

/** @test Objects can be removed from the pool */
START_TEST (test_pool_remove) {
    pool_t *pool;

    pool = proxy_pool_new(10);
    proxy_pool_remove(pool, 5);

    fail_unless(pool->size == 9);

    proxy_pool_destroy(pool);
} END_TEST

/** @test Objects can be fetched from the pool */
START_TEST (test_pool_get) {
    int i;

    i = proxy_pool_get(pool);

    fail_unless(i == 0);
    fail_unless(pool->avail[0] == FALSE);
} END_TEST

/** @test List of locked objects can be fetched */
START_TEST (test_pool_get_locked) {
    int i;

    proxy_pool_get(pool);
    i = proxy_pool_get_locked(pool);

    fail_unless(i == 0);
} END_TEST

/** @test Correct checking of free objects in pool */
START_TEST(test_pool_is_free) {
    int i;

    proxy_pool_lock(pool);

    fail_unless(proxy_pool_is_free(pool, 0));
    fail_unless(!proxy_pool_is_free(pool, 2));

    i = proxy_pool_get(pool);
    fail_unless(!proxy_pool_is_free(pool, i));
    proxy_pool_return(pool, i);

    proxy_pool_unlock(pool);
} END_TEST

/** @test Objects can be returned to the pool */
START_TEST (test_pool_return) {
    int i;

    i = proxy_pool_get(pool);
    proxy_pool_return(pool, i);

    fail_unless(pool->avail[i] == TRUE);
} END_TEST

Suite *pool_suite(void) {
    Suite *s = suite_create("Pool");

    TCase *tc_alloc = tcase_create("Allocation");
    tcase_add_checked_fixture(tc_alloc, setup, teardown);
    tcase_add_test(tc_alloc, test_pool_new);
    tcase_add_test(tc_alloc, test_pool_destroy_null);
    tcase_add_test(tc_alloc, test_pool_new_empty);
    suite_add_tcase(s, tc_alloc);

    TCase *tc_resize = tcase_create("Resize");
    tcase_add_checked_fixture(tc_resize, setup, teardown);
    tcase_add_test(tc_resize, test_pool_grow);
    tcase_add_test(tc_resize, test_pool_shrink);
    tcase_add_test(tc_resize, test_pool_remove);
    suite_add_tcase(s, tc_resize);

    TCase *tc_lock = tcase_create("Locking");
    tcase_add_checked_fixture(tc_lock, setup, teardown);
    tcase_add_test(tc_lock, test_pool_get);
    tcase_add_test(tc_lock, test_pool_get_locked);
    tcase_add_test(tc_lock, test_pool_is_free);
    tcase_add_test(tc_lock, test_pool_return);
    suite_add_tcase(s, tc_lock);

    return s;
}

int main(void) {
    int failed;
    Suite *s = pool_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
