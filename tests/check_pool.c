#include <check.h>
#include "../proxy.h"

START_TEST (test_pool_new) {
    pool_t *pool;

    pool = proxy_pool_new(1);

    fail_unless(pool->__alloc >=1);
    fail_unless(pool->size == 1);
    fail_unless(pool->avail[0] == TRUE);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_new_empty) {
    pool_t *pool;

    pool = proxy_pool_new(0);
    fail_unless(pool == NULL);
} END_TEST

START_TEST (test_pool_grow) {
    pool_t *pool;
    int i;

    pool = proxy_pool_new(1);
    proxy_pool_set_size(pool, 10);

    fail_unless(pool->size == 10);
    fail_unless(pool->__alloc >= 10);

    for (i=1; i<10; i++)
        fail_unless(pool->avail[i] == TRUE);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_shrink) {
    pool_t *pool;

    pool = proxy_pool_new(10);
    proxy_pool_set_size(pool, 1);

    fail_unless(pool->size == 1);
    fail_unless(pool->__alloc >= 1);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_remove) {
    pool_t *pool;

    pool = proxy_pool_new(10);
    proxy_pool_remove(pool, 5);

    fail_unless(pool->size == 9);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_get) {
    pool_t *pool;
    int i;

    pool = proxy_pool_new(1);
    i = proxy_get_from_pool(pool);

    fail_unless(i == 0);
    fail_unless(pool->avail[0] == FALSE);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_get_locked) {
    pool_t *pool;
    int i;

    pool = proxy_pool_new(1);
    proxy_get_from_pool(pool);

    i = proxy_pool_get_locked(pool);

    fail_unless(i == 0);

    proxy_pool_destroy(pool);
} END_TEST

START_TEST (test_pool_return) {
    pool_t *pool;
    int i;

    pool = proxy_pool_new(1);
    i = proxy_get_from_pool(pool);
    proxy_return_to_pool(pool, i);

    fail_unless(pool->avail[i] == TRUE);

    proxy_pool_destroy(pool);
} END_TEST

Suite *pool_suite(void) {
    Suite *s = suite_create("Pool");

    TCase *tc_alloc = tcase_create("Allocation");
    tcase_add_test(tc_alloc, test_pool_new);
    tcase_add_test(tc_alloc, test_pool_new_empty);
    suite_add_tcase(s, tc_alloc);

    TCase *tc_resize = tcase_create("Resize");
    tcase_add_test(tc_resize, test_pool_grow);
    tcase_add_test(tc_resize, test_pool_shrink);
    tcase_add_test(tc_resize, test_pool_remove);
    suite_add_tcase(s, tc_resize);

    TCase *tc_lock = tcase_create("Locking");
    tcase_add_test(tc_lock, test_pool_get);
    tcase_add_test(tc_lock, test_pool_get_locked);
    tcase_add_test(tc_lock, test_pool_return);
    suite_add_tcase(s, tc_lock);

    return s;
}

int main(void) {
    int failed;
    Suite *s = pool_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
