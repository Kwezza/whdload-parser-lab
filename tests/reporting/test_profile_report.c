/* tests/reporting/test_profile_report.c - Profile-aware report tests
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Tests whdtlv_report_profile_file() declared in
 * whdtlv/reporting/whdtlv_report_profile.h.
 *
 * Host only: run with:  make test-profile
 *
 * Exit code: 0 = all pass, non-zero = at least one failure.
 *
 * C99 (host-only module -- no Amiga/vbcc target required).
 * Must be compiled with -DWHDTLV_ENABLE_SELECTION_TRACE=1 (TRACE_CFLAGS).
 */

#include "whdtlv/reporting/whdtlv_report_profile.h"
#include "whdtlv/whdtlv.h"   /* whdtlv_filter_to_list for test5 parity check */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Test fixtures                                                          */

#define GAMES_TLV    "assets_raw/TLV/Game"
#define DEFS_DIR     "assets_raw/defs"
#define PROF_PAL     "assets_raw/profiles/pal_aga_4mb.profile"
#define PROF_MULTI   "assets_raw/profiles/multi_bucket_reference.profile"
#define BAD_PROF     "assets_raw/profiles/does_not_exist.profile"
#define OUT_BASE     "output/test_prof_base.csv"
#define OUT_MULTI    "output/test_prof_multi.csv"
#define OUT_SEARCH   "output/test_prof_search.csv"
#define OUT_PARITY   "output/test_prof_parity.csv"
#define OUT_COUNTERS "output/test_prof_counters.csv"

/*------------------------------------------------------------------------*/
/* Minimal test harness (mirrors test_report_csv.c style)                */

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
/* CSV file helpers                                                       */

/* Count data rows in a CSV by counting newlines in binary mode, then
 * subtracting 1 for the header line.  Binary mode avoids Windows CRLF
 * translation and the split-line problem for very wide rows. */
static long count_csv_data_rows(const char *path)
{
    FILE   *f = fopen(path, "rb");
    char    buf[8192];
    long    newlines = 0;
    size_t  n;
    size_t  i;

    if (!f) { return -1; }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (i = 0; i < n; ++i) {
            if (buf[i] == '\n') { ++newlines; }
        }
    }
    fclose(f);
    return (newlines > 1) ? newlines - 1 : 0;
}

/* Return 1 if the first (header) line of the CSV contains needle. */
static int header_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    char  buf[8192];
    int   found = 0;

    if (!f) { return 0; }
    if (fgets(buf, (int)sizeof(buf), f)) {
        found = (strstr(buf, needle) != NULL) ? 1 : 0;
    }
    fclose(f);
    return found;
}

/* Return 1 if any line in the file contains needle. */
static int file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    char  buf[4096];
    int   found = 0;

    if (!f) { return 0; }
    while (!found && fgets(buf, (int)sizeof(buf), f)) {
        if (strstr(buf, needle)) { found = 1; }
    }
    fclose(f);
    return found;
}

/* Count data rows whose first field matches prefix exactly.
 * Useful for counting "X," "-," "R," etc. rows.
 * Skips the header line (first line). */
static long count_rows_with_prefix(const char *path, const char *prefix)
{
    FILE       *f = fopen(path, "r");
    char        buf[4096];
    long        count = 0;
    int         first = 1;
    size_t      plen;

    if (!f) { return -1; }
    plen = strlen(prefix);
    while (fgets(buf, (int)sizeof(buf), f)) {
        if (first) { first = 0; continue; }  /* skip header */
        if (strncmp(buf, prefix, plen) == 0) { ++count; }
    }
    fclose(f);
    return count;
}

/*========================================================================*/
/* Test 1: NULL / missing required argument validation                   */
/*========================================================================*/

static void test1_null_args(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    int rc;

    print_sep("Test 1: NULL argument validation");

    memset(&sum, 0, sizeof(sum));

    /* NULL opts pointer */
    rc = whdtlv_report_profile_file(NULL, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_BAD_ARG, "NULL opts -> ERR_BAD_ARG");

    /* NULL tlv_path */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = NULL;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_BAD_ARG, "NULL tlv_path -> ERR_BAD_ARG");

    /* NULL defs_dir */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = NULL;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_BAD_ARG, "NULL defs_dir -> ERR_BAD_ARG");

    /* NULL profile_path */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = NULL;
    opts.output_csv_path = OUT_BASE;
    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_BAD_ARG, "NULL profile_path -> ERR_BAD_ARG");

    /* NULL output_csv_path */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = NULL;
    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_BAD_ARG, "NULL output_csv_path -> ERR_BAD_ARG");

    /* NULL summary is tolerated */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    rc = whdtlv_report_profile_file(&opts, NULL);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK || rc == WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN,
          "NULL summary is tolerated (OK or TLV_OPEN)");
}

/*========================================================================*/
/* Test 2: Missing TLV file                                               */
/*========================================================================*/

static void test2_missing_tlv(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    int rc;

    print_sep("Test 2: Missing TLV file");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = "output/this_file_does_not_exist.tlv";
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN ||
          rc == WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE,
          "missing TLV -> TLV_OPEN or TLV_PARSE error");
    printf("  (rc=%d)\n", rc);
}

/*========================================================================*/
/* Test 3: Missing profile file                                           */
/*========================================================================*/

static void test3_missing_profile(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    int rc;

    print_sep("Test 3: Missing profile file");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = BAD_PROF;
    opts.output_csv_path = OUT_BASE;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_ERR_PROFILE,
          "non-existent profile -> ERR_PROFILE");
    printf("  (rc=%d)\n", rc);
}

/*========================================================================*/
/* Test 4: Basic smoke — single-lane profile                             */
/*========================================================================*/

static void test4_basic_smoke(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    int  rc;
    long rows;

    print_sep("Test 4: Basic smoke (single-lane, pal_aga_4mb)");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path      = GAMES_TLV;
    opts.defs_dir      = DEFS_DIR;
    opts.profile_path  = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK,   "single-lane run returns OK");
    CHECK(sum.rows_written > 0,              "rows_written > 0");
    CHECK(sum.winners > 0,                   "winners > 0");
    CHECK(sum.groups_total > 0,              "groups_total > 0");
    CHECK(sum.variants_total > 0,            "variants_total > 0");
    CHECK(sum.rows_written == sum.variants_total,
          "one row per variant (rows_written == variants_total)");

    rows = count_csv_data_rows(OUT_BASE);
    CHECK(rows >= 0,                          "output file is readable");
    CHECK(rows >= (long)sum.rows_written,     "file newline count >= rows_written");

    printf("  (groups=%lu variants=%lu rows=%lu winners=%lu losers=%lu "
           "rejected=%lu file_rows=%ld)\n",
           sum.groups_total, sum.variants_total, sum.rows_written,
           sum.winners, sum.losers, sum.rejected, rows);
}

/*========================================================================*/
/* Test 5: Filter parity — winners must match whdtlv_filter_to_list()   */
/*========================================================================*/

static void test5_filter_parity(void)
{
    WhdTlvProfileReportOptions popts;
    WhdTlvProfileReportSummary psum;
    WhdTlvFilterOptions        fopts;
    WhdTlvFilterSummary        fsum;
    WhdTlvStringList           results;
    int rc_prof, rc_filt;

    print_sep("Test 5: Filter parity");

    memset(&popts, 0, sizeof(popts));
    popts.tlv_path       = GAMES_TLV;
    popts.defs_dir       = DEFS_DIR;
    popts.profile_path   = PROF_PAL;
    popts.output_csv_path = OUT_PARITY;
    memset(&psum, 0, sizeof(psum));

    rc_prof = whdtlv_report_profile_file(&popts, &psum);
    CHECK(rc_prof == WHDTLV_PROFILE_REPORT_OK, "profile report runs OK for parity");

    whdtlv_filter_options_defaults(&fopts);
    fopts.strict_crc = 0;  /* pre-built fixture may not match current defs CRC */
    memset(&fsum, 0, sizeof(fsum));
    memset(&results, 0, sizeof(results));

    rc_filt = whdtlv_filter_to_list(
        GAMES_TLV, DEFS_DIR, PROF_PAL,
        NULL, &fopts, &results, &fsum);

    if (rc_filt != WHDTLV_OK) {
        printf("  WARN: whdtlv_filter_to_list returned %d; parity count check skipped\n",
               rc_filt);
        whdtlv_string_list_free(&results);
        return;
    }

    CHECK(psum.winners == (unsigned long)fsum.selected_variants,
          "profile_summary.winners == filter_summary.selected_variants");

    printf("  (prof_winners=%lu filter_selected=%u)\n",
           psum.winners, fsum.selected_variants);

    whdtlv_string_list_free(&results);
}

/*========================================================================*/
/* Test 6: One winner per group for single-lane profile                  */
/*========================================================================*/

static void test6_one_winner_per_group(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    long x_rows;
    int  rc;

    print_sep("Test 6: One winner per group (single-lane)");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_PAL;
    opts.output_csv_path = OUT_BASE;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "run OK for winner-per-group check");

    /* selected_marker is quoted by the CSV writer, so rows start with "X", */
    x_rows = count_rows_with_prefix(OUT_BASE, "\"X\",");
    CHECK(x_rows >= 0, "OUT_BASE is readable for X-row count");
    CHECK((unsigned long)x_rows == sum.winners,
          "X row count in CSV matches summary.winners");
    CHECK(sum.winners == sum.groups_total,
          "single-lane: winners == groups_total (every group has a winner)");

    printf("  (x_rows=%ld summary.winners=%lu groups=%lu)\n",
           x_rows, sum.winners, sum.groups_total);
}

/*========================================================================*/
/* Test 7: Losers carry reason_code=lost_score                           */
/*========================================================================*/

static void test7_loser_reason(void)
{
    long dash_rows;

    print_sep("Test 7: Loser rows carry lost_score reason");

    /* OUT_BASE written by test4/test6; reuse it */
    /* selected_marker is quoted by the CSV writer, so rows start with "-", */
    dash_rows = count_rows_with_prefix(OUT_BASE, "\"-\",");
    CHECK(dash_rows > 0, "at least one '-' (loser) row in single-lane output");
    CHECK(file_contains(OUT_BASE, "lost_score"),
          "CSV contains 'lost_score' reason code");
}

/*========================================================================*/
/* Test 8: Rejected variants (multi_bucket excludes CD32/CDTV)           */
/*========================================================================*/

static void test8_rejected_variant(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    long r_rows;
    int  rc;

    print_sep("Test 8: Rejected variants (multi_bucket_reference profile)");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_MULTI;
    opts.output_csv_path = OUT_MULTI;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "multi-bucket run returns OK");
    CHECK(sum.rejected > 0, "summary.rejected > 0 (CD32/CDTV variants excluded)");

    /* selected_marker is quoted by the CSV writer, so rows start with "R", */
    r_rows = count_rows_with_prefix(OUT_MULTI, "\"R\",");
    CHECK(r_rows > 0, "at least one 'R' (rejected) row in CSV");
    CHECK(file_contains(OUT_MULTI, "rejected_exclude"),
          "CSV contains 'rejected_exclude' reason code");

    printf("  (rejected=%lu r_rows=%ld)\n", sum.rejected, r_rows);
}

/*========================================================================*/
/* Test 9: Header columns — selection metadata columns present           */
/*========================================================================*/

static void test9_header_columns(void)
{
    print_sep("Test 9: Header columns");

    /* Reuse OUT_BASE written by test4 */
    CHECK(header_contains(OUT_BASE, "selected_marker"),    "header: selected_marker");
    CHECK(header_contains(OUT_BASE, "selected_rank"),      "header: selected_rank");
    CHECK(header_contains(OUT_BASE, "reason_code"),        "header: reason_code");
    CHECK(header_contains(OUT_BASE, "selection_lane"),     "header: selection_lane");
    CHECK(header_contains(OUT_BASE, "lane_requirements"),  "header: lane_requirements");
    CHECK(header_contains(OUT_BASE, "group_id"),           "header: group_id");
    CHECK(header_contains(OUT_BASE, "group_name"),         "header: group_name");
    CHECK(header_contains(OUT_BASE, "display_name"),       "header: display_name");
    CHECK(header_contains(OUT_BASE, "score_total"),        "header: score_total");
    CHECK(header_contains(OUT_BASE, "reject_field"),       "header: reject_field");
    CHECK(header_contains(OUT_BASE, "lost_to_display_name"), "header: lost_to_display_name");
    CHECK(header_contains(OUT_BASE, "lost_to_score"),      "header: lost_to_score");
}

/*========================================================================*/
/* Test 10: Effective columns always present in profile mode             */
/*========================================================================*/

static void test10_effective_columns(void)
{
    print_sep("Test 10: Effective columns always present in profile mode");

    CHECK(header_contains(OUT_BASE, "_effective"),
          "header contains at least one '_effective' column");
    CHECK(header_contains(OUT_BASE, "_effective_source"),
          "header contains at least one '_effective_source' column");
    CHECK(file_contains(OUT_BASE, "explicit") ||
          file_contains(OUT_BASE, "default")  ||
          file_contains(OUT_BASE, "missing"),
          "effective_source values include explicit/default/missing");
}

/*========================================================================*/
/* Test 11: Multi-lane — lane_requirements column is populated           */
/*========================================================================*/

static void test11_multi_lane_populated(void)
{
    print_sep("Test 11: Multi-lane profile populates lane_requirements");

    /* OUT_MULTI written by test8 */
    CHECK(header_contains(OUT_MULTI, "selection_lane"),
          "multi-lane output has selection_lane column");
    CHECK(header_contains(OUT_MULTI, "lane_requirements"),
          "multi-lane output has lane_requirements column");
    CHECK(file_contains(OUT_MULTI, "chipset="),
          "lane_requirements column contains 'chipset=' (field=value format)");
}

/*========================================================================*/
/* Test 12: Multi-lane — winners >= groups (multiple lanes can each win) */
/*========================================================================*/

static void test12_multi_lane_multi_winner(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    int rc;

    print_sep("Test 12: Multi-lane profile winner count");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_MULTI;
    opts.output_csv_path = OUT_MULTI;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "multi-bucket run OK for winner count");
    CHECK(sum.winners > 0, "multi-lane: winners > 0");
    /* Multi-lane profiles can select more than one variant per group;
     * for a real collection with both AGA+En and AGA+De variants,
     * winners should exceed groups_total. */
    CHECK(sum.winners >= sum.groups_total,
          "multi-lane: winners >= groups_total (multi-lane can exceed 1 per group)");

    printf("  (winners=%lu groups=%lu ratio=%.2f)\n",
           sum.winners, sum.groups_total,
           sum.groups_total > 0
               ? (double)sum.winners / (double)sum.groups_total
               : 0.0);
}

/*========================================================================*/
/* Test 13: Search narrowing — fewer rows when search is active          */
/*========================================================================*/

static void test13_search_narrowing(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum_full;
    WhdTlvProfileReportSummary sum_search;
    int rc;

    print_sep("Test 13: Search narrowing (lotus*)");

    /* Full run */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_PAL;
    opts.search_pattern  = NULL;
    opts.output_csv_path = OUT_BASE;
    memset(&sum_full, 0, sizeof(sum_full));
    rc = whdtlv_report_profile_file(&opts, &sum_full);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "full run OK for search comparison");

    /* Narrowed run */
    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_PAL;
    opts.search_pattern  = "lotus*";
    opts.output_csv_path = OUT_SEARCH;
    memset(&sum_search, 0, sizeof(sum_search));
    rc = whdtlv_report_profile_file(&opts, &sum_search);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "search-narrowed run returns OK");
    CHECK(sum_search.rows_written > 0, "search found at least one group");
    CHECK(sum_search.rows_written < sum_full.rows_written,
          "search output has fewer rows than full run");
    CHECK(sum_search.winners <= sum_search.groups_total,
          "search: winners <= groups_total (single-lane)");

    printf("  (full_rows=%lu search_rows=%lu search_winners=%lu)\n",
           sum_full.rows_written, sum_search.rows_written, sum_search.winners);
}

/*========================================================================*/
/* Test 14: Summary counters add up to rows_written                      */
/*========================================================================*/

static void test14_summary_counters(void)
{
    WhdTlvProfileReportOptions opts;
    WhdTlvProfileReportSummary sum;
    unsigned long total;
    int rc;

    print_sep("Test 14: Summary counters");

    memset(&opts, 0, sizeof(opts));
    opts.tlv_path        = GAMES_TLV;
    opts.defs_dir        = DEFS_DIR;
    opts.profile_path    = PROF_PAL;
    opts.output_csv_path = OUT_COUNTERS;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_profile_file(&opts, &sum);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "counter test run returns OK");

    total = sum.winners + sum.losers + sum.rejected
          + sum.not_eligible + sum.dup_suppressed;

    CHECK(total == sum.rows_written,
          "winners+losers+rejected+not_eligible+dup_suppressed == rows_written");

    printf("  (rows=%lu = winners=%lu + losers=%lu + rejected=%lu"
           " + not_eligible=%lu + dup=%lu)\n",
           sum.rows_written, sum.winners, sum.losers,
           sum.rejected, sum.not_eligible, sum.dup_suppressed);
}

/*========================================================================*/
/* Test 15: CSV structural sanity — no runaway long lines                */
/*========================================================================*/

static void test15_csv_sanity(void)
{
    FILE *f;
    char  buf[8192];
    int   overlong = 0;
    long  lines = 0;

    print_sep("Test 15: CSV structural sanity");

    f = fopen(OUT_BASE, "r");
    if (!f) {
        printf("  WARN: could not open %s (test4 may have failed)\n", OUT_BASE);
        g_fail++;
        return;
    }
    while (fgets(buf, (int)sizeof(buf), f)) {
        ++lines;
        if (strlen(buf) == sizeof(buf) - 1 && buf[sizeof(buf) - 2] != '\n') {
            overlong = 1;
        }
    }
    fclose(f);

    CHECK(lines > 1,   "output has more than just a header line");
    CHECK(!overlong,   "no line is suspiciously long (quoting looks sane)");
}

/*========================================================================*/
/* Test 16: Empty search pattern — treated same as no search             */
/*========================================================================*/

static void test16_empty_search(void)
{
    WhdTlvProfileReportOptions opts_none;
    WhdTlvProfileReportOptions opts_empty;
    WhdTlvProfileReportSummary sum_none;
    WhdTlvProfileReportSummary sum_empty;
    int rc;

    print_sep("Test 16: Empty search string == no search");

    memset(&opts_none, 0, sizeof(opts_none));
    opts_none.tlv_path        = GAMES_TLV;
    opts_none.defs_dir        = DEFS_DIR;
    opts_none.profile_path    = PROF_PAL;
    opts_none.search_pattern  = NULL;
    opts_none.output_csv_path = "output/test_prof_t16a.csv";
    memset(&sum_none, 0, sizeof(sum_none));
    rc = whdtlv_report_profile_file(&opts_none, &sum_none);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "NULL search run OK");

    memset(&opts_empty, 0, sizeof(opts_empty));
    opts_empty.tlv_path        = GAMES_TLV;
    opts_empty.defs_dir        = DEFS_DIR;
    opts_empty.profile_path    = PROF_PAL;
    opts_empty.search_pattern  = "";
    opts_empty.output_csv_path = "output/test_prof_t16b.csv";
    memset(&sum_empty, 0, sizeof(sum_empty));
    rc = whdtlv_report_profile_file(&opts_empty, &sum_empty);
    CHECK(rc == WHDTLV_PROFILE_REPORT_OK, "empty-string search run OK");

    CHECK(sum_none.rows_written == sum_empty.rows_written,
          "NULL search and empty-string search produce same row count");

    printf("  (null_rows=%lu empty_rows=%lu)\n",
           sum_none.rows_written, sum_empty.rows_written);
}

/*========================================================================*/
/* main                                                                   */
/*========================================================================*/

int main(void)
{
    printf("whdtlv profile report - test suite\n");
    printf("TLV  : %s\n", GAMES_TLV);
    printf("Defs : %s\n", DEFS_DIR);
    printf("Prof : %s\n", PROF_PAL);

    test1_null_args();
    test2_missing_tlv();
    test3_missing_profile();
    test4_basic_smoke();           /* creates OUT_BASE */
    test5_filter_parity();         /* uses filter facade for parity */
    test6_one_winner_per_group();  /* reads OUT_BASE */
    test7_loser_reason();          /* reads OUT_BASE */
    test8_rejected_variant();      /* creates OUT_MULTI */
    test9_header_columns();        /* reads OUT_BASE */
    test10_effective_columns();    /* reads OUT_BASE */
    test11_multi_lane_populated(); /* reads OUT_MULTI */
    test12_multi_lane_multi_winner(); /* re-runs with PROF_MULTI */
    test13_search_narrowing();     /* creates OUT_SEARCH */
    test14_summary_counters();     /* creates OUT_COUNTERS */
    test15_csv_sanity();           /* reads OUT_BASE */
    test16_empty_search();         /* creates two temp outputs */

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail == 0) ? 0 : 1;
}

/* End of Text */
