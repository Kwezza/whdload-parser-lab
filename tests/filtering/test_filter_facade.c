/* tests/filtering/test_filter_facade.c - Facade harness (host and Amiga/vbcc)
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Tests the public filtering facade declared in include/whdtlv/whdtlv.h.
 * Only that public header is included -- no internal headers.
 *
 * Host:  make test-filter
 * Amiga: make TARGET=amiga test-filter  (builds; run on device with STACK 100000)
 *
 * Exit code: 0 = all pass, non-zero = at least one failure.
 *
 * C89-compatible; vbcc-safe.
 *   - All variables declared at block start.
 *   - No // comments, no C99 types, no VLAs, no for-loop initialisers.
 *   - Only #include "whdtlv/whdtlv.h" -- no internal headers.
 */

#include "whdtlv/whdtlv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Test paths (relative to the repo root where make is invoked)          */

#define GAMES_TLV     "output/Game(2026-04-17).tlv"
#define DEFS_DIR      "assets_raw/defs"
#define PROFILE_PAL   "assets_raw/profiles/pal_aga_4mb.profile"
#define PROFILE_MULTI "assets_raw/profiles/multi_bucket_reference.profile"
#define BAD_PROFILE   "assets_raw/profiles/this_does_not_exist.profile"
#define OUT_FILE_TMP  "output/test_facade_tmp.txt"

/*------------------------------------------------------------------------*/
/* Minimal test harness                                                   */

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

/*========================================================================*/
/* Test 1: Normal profile, no search                                      */
/*========================================================================*/

static void test1_normal_no_search(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    unsigned int        i;
    int                 rc;
    int                 all_nonempty;

    print_sep("Test 1: Normal profile, no search");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        NULL, &opts, &results, &summary);

    CHECK(rc == WHDTLV_OK,                          "returns WHDTLV_OK");
    CHECK(results.count > 0u,                        "results.count > 0");
    CHECK(results.count == summary.selected_variants,"count == selected_variants");
    CHECK(summary.groups_total > 0u,                 "groups_total > 0");
    CHECK(summary.matched_groups == summary.groups_total,
          "no-search: matched_groups == groups_total");

    all_nonempty = 1;
    for (i = 0u; i < results.count; i++) {
        if (!results.items[i] || results.items[i][0] == '\0') {
            all_nonempty = 0;
            break;
        }
    }
    CHECK(all_nonempty, "all returned strings non-NULL and non-empty");

    printf("  (variants=%u groups=%u selected=%u lanes=%u)\n",
           summary.variants_total, summary.groups_total,
           summary.selected_variants, summary.selection_lanes);

    whdtlv_string_list_free(&results);
    CHECK(results.count == 0u && results.items == NULL && results.reserved == NULL,
          "free resets struct to empty state");
}

/*========================================================================*/
/* Test 2: Search match (lotus*)                                          */
/*========================================================================*/

static void test2_search_match(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    int                 rc;

    print_sep("Test 2: Search match (lotus*)");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        "lotus*", &opts, &results, &summary);

    CHECK(rc == WHDTLV_OK,            "returns WHDTLV_OK");
    CHECK(summary.matched_groups > 0u, "matched_groups > 0");
    CHECK(results.count > 0u,          "results.count > 0");

    printf("  (matched_groups=%u selected=%u)\n",
           summary.matched_groups, results.count);

    whdtlv_string_list_free(&results);
}

/*========================================================================*/
/* Test 3: Search no-match                                                */
/*========================================================================*/

static void test3_search_no_match(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    int                 rc;

    print_sep("Test 3: Search no-match (thisshouldnotmatchanything*)");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        "thisshouldnotmatchanything*", &opts, &results, &summary);

    CHECK(rc == WHDTLV_OK,             "returns WHDTLV_OK");
    CHECK(results.count == 0u,          "results.count == 0");
    CHECK(summary.matched_groups == 0u, "matched_groups == 0");
    CHECK(results.items == NULL,        "items is NULL");
    CHECK(results.reserved == NULL,     "reserved is NULL");

    /* Safe to call free on empty result. */
    whdtlv_string_list_free(&results);
    CHECK(1, "free on empty result does not crash");
}

/*========================================================================*/
/* Test 4: Multi-lane profile                                             */
/*========================================================================*/

static void test4_multi_lane(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    int                 rc;

    print_sep("Test 4: Multi-lane profile (multi_bucket_reference)");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_MULTI,
        NULL, &opts, &results, &summary);

    CHECK(rc == WHDTLV_OK,                          "returns WHDTLV_OK");
    CHECK(summary.selection_lanes > 1u,              "selection_lanes > 1");
    CHECK(results.count == summary.selected_variants,"count == selected_variants");

    printf("  (lanes=%u selected=%u)\n",
           summary.selection_lanes, results.count);

    whdtlv_string_list_free(&results);
}

/*========================================================================*/
/* Test 5: Invalid profile path                                           */
/*========================================================================*/

static void test5_bad_profile(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    int                 rc;

    print_sep("Test 5: Invalid profile path");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, BAD_PROFILE,
        NULL, &opts, &results, &summary);

    CHECK(rc != WHDTLV_OK,      "returns failure code");
    CHECK(results.count == 0u,   "results.count == 0 after failure");
    CHECK(results.items == NULL, "items is NULL after failure");
    CHECK(results.reserved == NULL, "reserved is NULL after failure");

    printf("  (rc=%d)\n", rc);

    /* Free on already-empty struct must not crash. */
    whdtlv_string_list_free(&results);
    CHECK(1, "free on failure-state struct does not crash");
}

/*========================================================================*/
/* Test 6: whdtlv_string_list_free() safety                              */
/*========================================================================*/

static void test6_free_safety(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvStringList    results;
    WhdTlvStringList    empty;
    int                 rc;

    print_sep("Test 6: whdtlv_string_list_free() safety");

    /* Freeing a zeroed struct must not crash. */
    memset(&empty, 0, sizeof(empty));
    whdtlv_string_list_free(&empty);
    CHECK(1, "free of zeroed struct is safe");

    /* Freeing NULL must not crash. */
    whdtlv_string_list_free(NULL);
    CHECK(1, "free(NULL) is safe");

    /* Populate then free, then free again (struct was reset on first free). */
    whdtlv_filter_options_defaults(&opts);
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        NULL, &opts, &results, NULL);

    if (rc == WHDTLV_OK && results.count > 0u) {
        whdtlv_string_list_free(&results);
        CHECK(results.reserved == NULL, "reserved is NULL after first free");
        /* Second free on already-zeroed struct must not crash. */
        whdtlv_string_list_free(&results);
        CHECK(1, "double-free via zeroed struct is safe");
    } else {
        /* If the TLV is absent or returns empty, still verify null safety. */
        whdtlv_string_list_free(&results);
        CHECK(1, "free on empty/failed result is safe (TLV may be absent)");
        CHECK(1, "double-free placeholder (populated list not available)");
    }
}

/*========================================================================*/
/* Test 7: whdtlv_filter_to_file() wrapper                               */
/*========================================================================*/

static void test7_filter_to_file(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    FILE               *f;
    unsigned int        line_count;
    char                buf[512];
    int                 rc;

    print_sep("Test 7: whdtlv_filter_to_file() wrapper");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));

    rc = whdtlv_filter_to_file(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        OUT_FILE_TMP, NULL, &opts, &summary);

    CHECK(rc == WHDTLV_OK, "returns WHDTLV_OK");

    /* Count lines in the output file and compare against selected_variants. */
    line_count = 0u;
    f = fopen(OUT_FILE_TMP, "r");
    if (f) {
        while (fgets(buf, (int)sizeof(buf), f)) {
            if (buf[0] != '\0' && buf[0] != '\n') {
                line_count++;
            }
        }
        fclose(f);
        CHECK(line_count == summary.selected_variants,
              "file line count == selected_variants");
    } else {
        printf("  WARN: could not open output file %s\n", OUT_FILE_TMP);
        /* Still count this as a failure. */
        g_fail++;
    }

    printf("  (lines=%u selected=%u)\n", line_count, summary.selected_variants);
}

/*========================================================================*/
/* Test 8: Empty search pattern treated as no search                     */
/*========================================================================*/

static void test8_empty_search_pattern(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    int                 rc;

    print_sep("Test 8: Empty search pattern treated as no search");

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROFILE_PAL,
        "", &opts, &results, &summary);

    CHECK(rc == WHDTLV_OK, "returns WHDTLV_OK");
    CHECK(summary.matched_groups == summary.groups_total,
          "empty pattern: matched_groups == groups_total");

    printf("  (groups_total=%u matched_groups=%u selected=%u)\n",
           summary.groups_total, summary.matched_groups, results.count);

    whdtlv_string_list_free(&results);
    CHECK(1, "free after empty-pattern run does not crash");
}

/*========================================================================*/
/* Test 9: NULL and empty profile_path (built-in default scoring)         */
/*========================================================================*/

static void test9_null_and_empty_profile(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary_a;
    WhdTlvFilterSummary summary_b;
    WhdTlvStringList    results_a;
    WhdTlvStringList    results_b;
    unsigned int        count_a;
    unsigned int        count_b;
    int                 rc;

    print_sep("Test 9: NULL and empty profile_path (default scoring)");

    /* Sub-run A: profile_path = NULL */
    whdtlv_filter_options_defaults(&opts);
    memset(&summary_a, 0, sizeof(summary_a));
    memset(&results_a, 0, sizeof(results_a));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, NULL,
        NULL, &opts, &results_a, &summary_a);

    CHECK(rc == WHDTLV_OK,       "NULL profile: returns WHDTLV_OK");
    CHECK(results_a.count > 0u,  "NULL profile: results non-empty");
    count_a = results_a.count;

    whdtlv_string_list_free(&results_a);

    /* Sub-run B: profile_path = "" */
    whdtlv_filter_options_defaults(&opts);
    memset(&summary_b, 0, sizeof(summary_b));
    memset(&results_b, 0, sizeof(results_b));

    rc = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, "",
        NULL, &opts, &results_b, &summary_b);

    CHECK(rc == WHDTLV_OK,       "empty-string profile: returns WHDTLV_OK");
    CHECK(results_b.count > 0u,  "empty-string profile: results non-empty");
    count_b = results_b.count;

    /* Both must take the same has_profile=0 branch -> identical count */
    CHECK(count_a == count_b,    "NULL and empty-string profile produce same count");

    printf("  (count_null=%u count_empty=%u)\n", count_a, count_b);

    whdtlv_string_list_free(&results_b);
}

/*========================================================================*/
/* main                                                                   */
/*========================================================================*/

int main(void)
{
    printf("whdtlv public filter facade - test suite\n");
    printf("TLV  : %s\n", GAMES_TLV);
    printf("Defs : %s\n", DEFS_DIR);
    printf("Prof : %s\n", PROFILE_PAL);

    test1_normal_no_search();
    test2_search_match();
    test3_search_no_match();
    test4_multi_lane();
    test5_bad_profile();
    test6_free_safety();
    test7_filter_to_file();
    test8_empty_search_pattern();
    test9_null_and_empty_profile();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail == 0) ? 0 : 1;
}

/* End of Text */
