/******************************************************************************
 * check_map.c
 *
 * Query mapper tests
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

#include "../config.h"

#include <check.h>
#include "../map/proxy_map.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ltdl.h>

proxy_map_query_t func = NULL;
lt_dlhandle handle;

void setup_rowa() {
    fail_unless(lt_dlinit() == 0);

    if (!(handle = lt_dlopenext("../map/" LT_OBJDIR "libproxymap-rowa"))) {
        func = NULL;
        return;
    }

    func = (proxy_map_query_t) (intptr_t) lt_dlsym(handle, "proxy_map_query");

    if (lt_dlerror() != NULL) {
        func = NULL;
        return;
    }
}

void teardown() {
    lt_dlexit();
}

START_TEST (test_rowa_read) {
    proxy_query_map_t *map;

    map = (*func)("SELECT 1;");
    fail_unless(map->map == QUERY_MAP_ANY);

    map = (*func)("SHOW TABLES;");
    fail_unless(map->map == QUERY_MAP_ANY);

    map = (*func)("DESCRIBE TABLE `test`;");
    fail_unless(map->map == QUERY_MAP_ANY);

    map = (*func)("EXPLAIN SELECT 1;");
    fail_unless(map->map == QUERY_MAP_ANY);
} END_TEST

START_TEST (test_rowa_other) {
    proxy_query_map_t *map = (*func)("INSERT INTO test VALUES(1);");

    fail_unless(map->map == QUERY_MAP_ALL);
} END_TEST

Suite *map_suite(void) {
    Suite *s = suite_create("Mapping");

    TCase *tc_rowa = tcase_create("Read one, Write all");
    tcase_add_checked_fixture(tc_rowa, setup_rowa, teardown);
    tcase_add_test(tc_rowa, test_rowa_read);
    tcase_add_test(tc_rowa, test_rowa_other);
    suite_add_tcase(s, tc_rowa);

    return s;
}

int main(void) {
    int failed;
    Suite *s = map_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
