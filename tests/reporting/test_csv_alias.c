/* tests/reporting/test_csv_alias.c - CSV duplicate-ID alias tests
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Proves that duplicate-ID alias rows in a CSV file:
 *   - All resolve correctly by forward (token -> ID) lookup.
 *   - Reverse (ID -> token/description) lookup always returns the
 *     canonical first row, never an alias row.
 *
 * Fixture: tests/fixtures/defs/Alias.csv
 *   7,UNKNOWN512K,512 KB memory (type unknown),default
 *   7,512k,Alias for UNKNOWN512K
 *   7,512kb,Alias for UNKNOWN512K
 *
 * Host only (C99). Run with:  make test-csv-alias
 *
 * Exit code: 0 = all pass, non-zero = at least one failure.
 */

#include "whdtlv/core/csv_cache.h"

#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Harness                                                                */

#define FIXTURE_DIR "tests/fixtures/defs"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s\n", msg); \
            g_fail++; \
        } \
    } while (0)

static void print_sep(const char *name)
{
    printf("\n--- %s ---\n", name);
}

/*------------------------------------------------------------------------*/
/* Fixture                                                                */

static GlobalCSVManager g_mgr;
static CSVCache        *g_cache = NULL;

static int setup(void)
{
    memset(&g_mgr, 0, sizeof(g_mgr));
    if (!csv_cache_manager_init(&g_mgr, NULL, FIXTURE_DIR)) {
        printf("ERROR: csv_cache_manager_init failed for '%s'\n", FIXTURE_DIR);
        return 0;
    }
    g_cache = csv_cache_get_or_load(&g_mgr, "Alias");
    if (!g_cache) {
        printf("ERROR: could not load Alias.csv from '%s/Alias.csv'\n", FIXTURE_DIR);
        return 0;
    }
    printf("Alias.csv loaded (%d entries)\n", (int)g_cache->entry_count);
    return 1;
}

static void teardown(void)
{
    csv_cache_manager_cleanup(&g_mgr);
}

/*========================================================================*/
/* Test 1 — Forward lookup: all tokens resolve to the same numeric ID    */
/*========================================================================*/

static void test1_forward_lookup(void)
{
    uint32_t id_canon;
    uint32_t id_alias1;
    uint32_t id_alias2;

    print_sep("Test 1: Forward lookup (token -> ID)");

    id_canon  = csv_cache_lookup_loaded(g_cache, "UNKNOWN512K");
    id_alias1 = csv_cache_lookup_loaded(g_cache, "512k");
    id_alias2 = csv_cache_lookup_loaded(g_cache, "512kb");

    CHECK(id_canon  == 7u, "UNKNOWN512K -> 7");
    CHECK(id_alias1 == 7u, "512k        -> 7");
    CHECK(id_alias2 == 7u, "512kb       -> 7");
    CHECK(id_canon == id_alias1, "canonical and alias1 share the same ID");
    CHECK(id_canon == id_alias2, "canonical and alias2 share the same ID");
}

/*========================================================================*/
/* Test 2 — All alias rows are loaded (entry_count reflects all rows)    */
/*========================================================================*/

static void test2_all_rows_loaded(void)
{
    print_sep("Test 2: All alias rows loaded into the hash table");
    CHECK(g_cache->entry_count == 3u,
          "entry_count == 3 (canonical + 2 aliases, all loaded for forward lookup)");
}

/*========================================================================*/
/* Test 3 — Reverse lookup returns the canonical token and description   */
/*========================================================================*/

static void test3_reverse_lookup_token(void)
{
    const char *tok;

    print_sep("Test 3: Reverse lookup returns canonical token (lowercased)");

    tok = csv_cache_reverse_lookup(&g_mgr, "Alias", 7u, 0 /* want_long = false */);

    CHECK(tok != NULL, "reverse lookup returns non-NULL token for ID 7");
    /* Tokens are stored lowercased; first CSV row 'UNKNOWN512K' -> 'unknown512k' */
    CHECK(tok && strcmp(tok, "unknown512k") == 0,
          "reverse token is 'unknown512k' (canonical row, stored lowercase)");
}

static void test4_reverse_lookup_description(void)
{
    const char *desc;

    print_sep("Test 4: Reverse lookup returns canonical description");

    desc = csv_cache_reverse_lookup(&g_mgr, "Alias", 7u, 1 /* want_long = true */);

    CHECK(desc != NULL, "reverse lookup returns non-NULL description for ID 7");
    CHECK(desc && strcmp(desc, "512 KB memory (type unknown)") == 0,
          "reverse description matches canonical long_name");
}

/*========================================================================*/
/* Test 5 — Alias tokens are not returned by reverse lookup              */
/*========================================================================*/

static void test5_aliases_not_returned(void)
{
    const char *tok;

    print_sep("Test 5: Alias tokens are never returned by reverse lookup");

    tok = csv_cache_reverse_lookup(&g_mgr, "Alias", 7u, 0);

    CHECK(tok && strcmp(tok, "512k")  != 0,
          "reverse does not return '512k'  (alias row)");
    CHECK(tok && strcmp(tok, "512kb") != 0,
          "reverse does not return '512kb' (alias row)");
}

/*========================================================================*/
/* Test 6 — Default marker on canonical row                              */
/*========================================================================*/

static void test6_default_marker(void)
{
    bool     has_def;
    uint32_t def_id;

    print_sep("Test 6: Default marker behaviour");

    /* Alias.csv: only the canonical row carries 'default' */
    has_def = false;
    def_id  = csv_cache_get_default_token(&g_mgr, "Alias", &has_def);

    CHECK(has_def,        "has_default_token is set");
    CHECK(def_id == 7u,   "default_token_id == 7 (canonical row's ID)");
}

/*========================================================================*/
/* main                                                                  */
/*========================================================================*/

int main(void)
{
    printf("=== test_csv_alias ===\n");

    if (!setup()) {
        printf("FATAL: fixture setup failed — check %s/Alias.csv exists.\n", FIXTURE_DIR);
        return 1;
    }

    test1_forward_lookup();
    test2_all_rows_loaded();
    test3_reverse_lookup_token();
    test4_reverse_lookup_description();
    test5_aliases_not_returned();
    test6_default_marker();

    teardown();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
