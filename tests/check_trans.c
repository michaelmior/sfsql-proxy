/******************************************************************************
 * check_trans.c
 *
 * Transaction management tests.
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

#include "hashtable/hashtable.c"
#include "proxy_trans.c"

#include <check.h>

/** Fixture to initialize the hashtable for tests. */
void setup() {
    options.two_pc = options.coordinator = options.add_ids = TRUE;
    proxy_trans_init();
}

/** Fixture to destroy the hashtable after tests. */
void teardown() {
    proxy_trans_end();
}

/** @test Equality function for keys */
START_TEST (test_trans_key_eq) {
    ulong key1, key2;

    key1 = key2 = 1;
    fail_unless(trans_eq(&key1, &key2));

    key1 = 1; key2 = 2;
    fail_unless(!trans_eq(&key1, &key2));
} END_TEST

/** @test Different keys produce different hashes */
START_TEST (test_trans_hash_diff) {
    ulong key1, key2;
    unsigned int hash1, hash2;

    key1 = 1; key2 = 2;
    hash1 = trans_hash(&key1);
    hash2 = trans_hash(&key2);

    fail_unless(hash1 != hash2);
} END_TEST

/** @test Same keys produce same hashes */
START_TEST (test_trans_hash_eq) {
    ulong key1, key2;
    unsigned int hash1, hash2;

    key1 = 1; key2 = 1;
    hash1 = trans_hash(&key1);
    hash2 = trans_hash(&key2);

    fail_unless(hash1 == hash2);
} END_TEST

/** @test Add new transaction to hashtable */
START_TEST (test_trans_add) {
    ulong *key = (ulong*) malloc(sizeof(ulong));
    proxy_trans_t *trans = (proxy_trans_t*) malloc(sizeof(proxy_trans_t));

    *key = 1;
    proxy_trans_insert(key, trans);
    fail_unless(hashtable_count(trans_table) == 1);
    fail_unless(proxy_trans_search(key) == trans);
} END_TEST

/** @test Remove a transaction from the hashtable */
START_TEST (test_trans_remove) {
    ulong *key = (ulong*) malloc(sizeof(ulong));
    proxy_trans_t *trans = (proxy_trans_t*) malloc(sizeof(proxy_trans_t));

    *key = 1;
    proxy_trans_insert(key, trans);
    fail_unless(proxy_trans_remove(key) == trans);
    fail_unless(hashtable_count(trans_table) == 0);
} END_TEST

Suite *pool_suite(void) {
    Suite *s = suite_create("Transaction management");

    TCase *tc_funcs = tcase_create("Functions");
    tcase_add_test(tc_funcs, test_trans_key_eq);
    tcase_add_test(tc_funcs, test_trans_hash_diff);
    tcase_add_test(tc_funcs, test_trans_hash_eq);
    suite_add_tcase(s, tc_funcs);

    TCase *tc_hashtable = tcase_create("Hashtable");
    tcase_add_checked_fixture(tc_hashtable, setup, teardown);
    tcase_add_test(tc_hashtable, test_trans_add);
    tcase_add_test(tc_hashtable, test_trans_remove);
    suite_add_tcase(s, tc_hashtable);

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
