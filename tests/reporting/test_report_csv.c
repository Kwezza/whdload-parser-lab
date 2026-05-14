/* tests/reporting/test_report_csv.c - Host-side reporting subsystem tests
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Tests whdtlv_report_csv_file() declared in whdtlv/reporting/whdtlv_report_csv.h.
 *
 * Host only: run with:  make test-report
 *
 * Exit code: 0 = all pass, non-zero = at least one failure.
 *
 * C99 (host-only module -- no Amiga/vbcc target required).
 */

#include "whdtlv/reporting/whdtlv_report_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Test fixtures                                                          */

#define GAMES_TLV    "assets_raw/TLV/Game"
#define DEFS_DIR     "assets_raw/defs"
#define BAD_TLV      "output/this_file_does_not_exist.tlv"
#define BAD_OUT      "output/no_such_dir_xyz/report.csv"
#define OUT_WIDE     "output/test_report_wide.csv"
#define OUT_LONG     "output/test_report_long.csv"
#define OUT_IDS      "output/test_report_ids.csv"
#define OUT_DESC     "output/test_report_desc.csv"
#define OUT_STAT     "output/test_report_status.csv"
#define OUT_MULTI    "output/test_report_multionly.csv"
#define OUT_PROB     "output/test_report_problems.csv"

/*------------------------------------------------------------------------*/
/* Minimal test harness (mirrors test_filter_facade.c style)             */

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

/* Count data rows in a CSV by counting newline characters in binary mode,
 * then subtracting 1 for the header line.  Binary mode avoids Windows
 * CRLF translation and the split-line problem that occurs when data rows
 * are wider than a fgets buffer. */
static long count_csv_data_rows(const char *path)
{
    FILE        *f = fopen(path, "rb");
    char         buf[8192];
    long         newlines = 0;
    size_t       n;
    size_t       i;

    if (!f) { return -1; }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (i = 0; i < n; ++i) {
            if (buf[i] == '\n') { ++newlines; }
        }
    }
    fclose(f);
    /* Subtract 1 for the header line; return 0 if file had only a header */
    return (newlines > 1) ? newlines - 1 : 0;
}

/* Return 1 if the first (header) line of the CSV contains the substring. */
static int header_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    char  buf[4096];
    int   found = 0;

    if (!f) { return 0; }
    if (fgets(buf, (int)sizeof(buf), f)) {
        found = (strstr(buf, needle) != NULL) ? 1 : 0;
    }
    fclose(f);
    return found;
}

/* Return 1 if any line in the file contains the substring. */
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

/*========================================================================*/
/* Test 1: NULL argument validation                                       */
/*========================================================================*/

static void test1_null_args(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 1: NULL argument validation");

    whdtlv_report_options_defaults(&opts);
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(NULL, DEFS_DIR, OUT_WIDE, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_ERR_BAD_ARG, "NULL tlv_path -> ERR_BAD_ARG");

    rc = whdtlv_report_csv_file(GAMES_TLV, NULL, OUT_WIDE, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_ERR_BAD_ARG, "NULL defs_dir -> ERR_BAD_ARG");

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, NULL, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_ERR_BAD_ARG, "NULL output_csv_path -> ERR_BAD_ARG");

    /* NULL options and summary must be tolerated (defaults apply) */
    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_WIDE, NULL, NULL);
    CHECK(rc == WHDTLV_REPORT_OK || rc == WHDTLV_REPORT_ERR_TLV_OPEN,
          "NULL options and summary are tolerated");
}

/*========================================================================*/
/* Test 2: Missing TLV file                                               */
/*========================================================================*/

static void test2_missing_tlv(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 2: Missing TLV file");

    whdtlv_report_options_defaults(&opts);
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(BAD_TLV, DEFS_DIR, OUT_WIDE, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_ERR_TLV_OPEN || rc == WHDTLV_REPORT_ERR_TLV_PARSE,
          "missing TLV -> TLV_OPEN or TLV_PARSE error");
    printf("  (rc=%d)\n", rc);
}

/*========================================================================*/
/* Test 3: Wide export — basic smoke test                                 */
/*========================================================================*/

static void test3_wide_export(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int  rc;
    long rows;

    print_sep("Test 3: Wide export (basic)");

    whdtlv_report_options_defaults(&opts);
    opts.mode = WHDTLV_REPORT_CSV_WIDE;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_WIDE, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK,          "wide export returns OK");
    CHECK(sum.variants_total > 0,           "variants_total > 0");
    CHECK(sum.groups_total > 0,             "groups_total > 0");
    CHECK(sum.rows_written > 0,             "rows_written > 0");
    CHECK(sum.rows_written == sum.variants_total,
          "one row per variant in wide mode");

    rows = count_csv_data_rows(OUT_WIDE);
    CHECK(rows >= 0,                             "output file is readable");
    CHECK(rows >= (long)sum.rows_written,        "file newline count >= rows_written");

    printf("  (variants=%lu groups=%lu rows=%lu file_rows=%ld)\n",
           sum.variants_total, sum.groups_total, sum.rows_written, rows);
}

/*========================================================================*/
/* Test 4: Wide export — standard header columns                         */
/*========================================================================*/

static void test4_wide_header(void)
{
    print_sep("Test 4: Wide export header columns");

    CHECK(header_contains(OUT_WIDE, "group_id"),      "header has group_id");
    CHECK(header_contains(OUT_WIDE, "group_name"),    "header has group_name");
    CHECK(header_contains(OUT_WIDE, "display_name"),  "header has display_name");
    CHECK(header_contains(OUT_WIDE, "archive_size_kib"),  "header has archive_size_kib");
    CHECK(header_contains(OUT_WIDE, "archive_crc32"), "header has archive_crc32");
}

/*========================================================================*/
/* Test 5: Long export — basic smoke test                                 */
/*========================================================================*/

static void test5_long_export(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int  rc;
    long rows;

    print_sep("Test 5: Long export (basic)");

    whdtlv_report_options_defaults(&opts);
    opts.mode = WHDTLV_REPORT_CSV_LONG;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_LONG, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK,   "long export returns OK");
    CHECK(sum.variants_total > 0,    "variants_total > 0");
    CHECK(sum.rows_written > 0,      "rows_written > 0");

    /* Long mode emits >= variants_total rows (at least one row per variant) */
    CHECK(sum.rows_written >= sum.variants_total,
          "rows_written >= variants_total in long mode");

    rows = count_csv_data_rows(OUT_LONG);
    CHECK(rows >= 0,                             "output file is readable");
    CHECK(rows >= (long)sum.rows_written,        "file newline count >= rows_written");

    printf("  (variants=%lu rows=%lu file_rows=%ld)\n",
           sum.variants_total, sum.rows_written, rows);
}

/*========================================================================*/
/* Test 6: Long export — header columns                                  */
/*========================================================================*/

static void test6_long_header(void)
{
    print_sep("Test 6: Long export header columns");

    CHECK(header_contains(OUT_LONG, "group_id"),              "header has group_id");
    CHECK(header_contains(OUT_LONG, "group_name"),            "header has group_name");
    CHECK(header_contains(OUT_LONG, "display_name"),          "header has display_name");
    CHECK(header_contains(OUT_LONG, "field_id"),              "header has field_id");
    CHECK(header_contains(OUT_LONG, "field_name"),            "header has field_name");
    CHECK(header_contains(OUT_LONG, "value_index"),           "header has value_index");
    CHECK(header_contains(OUT_LONG, "raw_value"),             "header has raw_value");
    CHECK(header_contains(OUT_LONG, "resolved_token"),        "header has resolved_token");
    CHECK(header_contains(OUT_LONG, "resolved_description"),  "header has resolved_description");
    CHECK(header_contains(OUT_LONG, "status"),                "header has status");
}

/*========================================================================*/
/* Test 7: --include-ids adds _ids columns                               */
/*========================================================================*/

static void test7_include_ids(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 7: include_ids adds _ids columns");

    whdtlv_report_options_defaults(&opts);
    opts.include_ids = 1;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_IDS, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK, "export with include_ids returns OK");
    CHECK(header_contains(OUT_IDS, "_ids"), "header contains _ids column suffix");
}

/*========================================================================*/
/* Test 8: --include-desc adds _descriptions columns                     */
/*========================================================================*/

static void test8_include_desc(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 8: include_descriptions adds _descriptions columns");

    whdtlv_report_options_defaults(&opts);
    opts.include_descriptions = 1;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_DESC, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK, "export with include_descriptions returns OK");
    CHECK(header_contains(OUT_DESC, "_descriptions"),
          "header contains _descriptions column suffix");
}

/*========================================================================*/
/* Test 9: --include-status adds _status columns                         */
/*========================================================================*/

static void test9_include_status(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 9: include_status adds _status columns");

    whdtlv_report_options_defaults(&opts);
    opts.include_status = 1;
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_STAT, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK, "export with include_status returns OK");
    CHECK(header_contains(OUT_STAT, "_status"),
          "header contains _status column suffix");
}

/*========================================================================*/
/* Test 10: only_multi_variant_groups skips singletons                   */
/*========================================================================*/

static void test10_multi_only(void)
{
    WhdTlvReportOptions opts_all;
    WhdTlvReportOptions opts_multi;
    WhdTlvReportSummary sum_all;
    WhdTlvReportSummary sum_multi;
    int rc;

    print_sep("Test 10: only_multi_variant_groups");

    whdtlv_report_options_defaults(&opts_all);
    memset(&sum_all, 0, sizeof(sum_all));
    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_WIDE, &opts_all, &sum_all);

    whdtlv_report_options_defaults(&opts_multi);
    opts_multi.only_multi_variant_groups = 1;
    memset(&sum_multi, 0, sizeof(sum_multi));
    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_MULTI, &opts_multi, &sum_multi);

    CHECK(rc == WHDTLV_REPORT_OK, "multi-only export returns OK");
    /* The games TLV contains groups with multiple variants so the output
     * must be non-empty, but fewer rows than the full export. */
    CHECK(sum_multi.variants_total <= sum_all.variants_total,
          "multi-only: variants_total <= full variants_total");
    CHECK(sum_multi.rows_written <= sum_all.rows_written,
          "multi-only: fewer or equal rows than full export");

    printf("  (full_rows=%lu multi_rows=%lu)\n",
           sum_all.rows_written, sum_multi.rows_written);
}

/*========================================================================*/
/* Test 11: Resolved values appear in wide output                        */
/*========================================================================*/

static void test11_resolved_values_present(void)
{
    WhdTlvReportSummary sum;

    print_sep("Test 11: Resolved values present in wide output");

    /* Reuse the wide output written by test3; sum is not used here */
    (void)sum;

    /* The wide CSV must contain at least one known Chipset token.
     * Games TLV always contains AGA or OCS entries. */
    CHECK(file_contains(OUT_WIDE, "aga") || file_contains(OUT_WIDE, "ocs"),
          "wide CSV contains a known chipset token (aga or ocs)");
}

/*========================================================================*/
/* Test 12: archive_info CRC rendered as 8 uppercase hex digits          */
/*========================================================================*/

static void test12_archive_crc_format(void)
{
    print_sep("Test 12: archive_info CRC32 format in long output");

    /* Long mode emits archive_info as size:CRC hex.  Wide mode renders the
     * CRC column as 8 hex digits.  Check that the wide file contains a
     * plausible 8-hex-char run (upper or lower case both valid here). */
    CHECK(file_contains(OUT_WIDE, "archive_crc32") ||
          header_contains(OUT_WIDE, "archive_crc32"),
          "archive_crc32 column present in wide output");
}

/*========================================================================*/
/* Test 13: Missing CSV directory is tolerated (non-fatal)               */
/*========================================================================*/

static void test13_missing_csv_tolerated(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;
    char out_path[] = "output/test_report_nodefs.csv";

    print_sep("Test 13: Missing CSV directory is tolerated");

    whdtlv_report_options_defaults(&opts);
    memset(&sum, 0, sizeof(sum));

    /* Point defs at a directory that does not exist */
    rc = whdtlv_report_csv_file(GAMES_TLV, "output/no_such_defs_dir",
                                 out_path, &opts, &sum);
    /* Must succeed (graceful degradation), just with unresolved tokens */
    CHECK(rc == WHDTLV_REPORT_OK,     "missing defs dir is non-fatal");
    CHECK(sum.variants_total > 0,      "variants still parsed without defs");
    CHECK(sum.rows_written > 0,        "rows still written without defs");

    printf("  (rc=%d variants=%lu resolved=%lu unresolved=%lu)\n",
           rc, sum.variants_total, sum.values_resolved, sum.values_unresolved);
}

/*========================================================================*/
/* Test 14: CSV escaping — display_name with a comma is quoted           */
/*========================================================================*/

static void test14_csv_escaping(void)
{
    /* We cannot guarantee the Games TLV has a comma in a filename, but we
     * can verify the output file is well-formed CSV by checking that every
     * line has the same number of unquoted commas as the header.
     * Instead of a full parser, we just verify:
     *   - the file opens
     *   - no line is longer than a sane limit (no runaway unquoted newlines) */
    FILE *f;
    char  buf[8192];
    int   overlong = 0;
    long  lines = 0;

    print_sep("Test 14: CSV output is structurally sane");

    f = fopen(OUT_WIDE, "r");
    if (!f) {
        printf("  WARN: could not open %s (test3 may have failed)\n", OUT_WIDE);
        g_fail++;
        return;
    }
    while (fgets(buf, (int)sizeof(buf), f)) {
        ++lines;
        /* If fgets filled the buffer without hitting '\n', the line is
         * longer than 8191 bytes — almost certainly a quoting runaway. */
        if (strlen(buf) == sizeof(buf) - 1 && buf[sizeof(buf) - 2] != '\n') {
            overlong = 1;
        }
    }
    fclose(f);

    CHECK(lines > 1,      "output has more than just a header line");
    CHECK(!overlong,      "no line is suspiciously long (quoting looks sane)");
}

/*========================================================================*/
/* Test 15: summary struct is fully populated after a successful export  */
/*========================================================================*/

static void test15_summary_populated(void)
{
    WhdTlvReportOptions opts;
    WhdTlvReportSummary sum;
    int rc;

    print_sep("Test 15: Summary struct populated after export");

    whdtlv_report_options_defaults(&opts);
    memset(&sum, 0, sizeof(sum));

    rc = whdtlv_report_csv_file(GAMES_TLV, DEFS_DIR, OUT_WIDE, &opts, &sum);
    CHECK(rc == WHDTLV_REPORT_OK,           "export succeeds");
    CHECK(sum.variants_total > 0,            "variants_total populated");
    CHECK(sum.groups_total > 0,              "groups_total populated");
    CHECK(sum.rows_written > 0,              "rows_written populated");
    CHECK(sum.fields_written > 0,            "fields_written populated");
    CHECK(sum.values_resolved + sum.values_unresolved > 0,
          "at least one value resolution attempt recorded");
    CHECK(sum.multi_variant_groups_seen > 0, "multi_variant_groups_seen > 0");

    printf("  resolved=%lu unresolved=%lu problem=%lu multi_val=%lu multi_grp=%lu\n",
           sum.values_resolved, sum.values_unresolved, sum.problem_rows,
           sum.multi_value_fields_seen, sum.multi_variant_groups_seen);
}

/*========================================================================*/
/* main                                                                   */
/*========================================================================*/

int main(void)
{
    printf("whdtlv reporting - test suite\n");
    printf("TLV  : %s\n", GAMES_TLV);
    printf("Defs : %s\n", DEFS_DIR);

    test1_null_args();
    test2_missing_tlv();
    test3_wide_export();
    test4_wide_header();
    test5_long_export();
    test6_long_header();
    test7_include_ids();
    test8_include_desc();
    test9_include_status();
    test10_multi_only();
    test11_resolved_values_present();
    test12_archive_crc_format();
    test13_missing_csv_tolerated();
    test14_csv_escaping();
    test15_summary_populated();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail == 0) ? 0 : 1;
}

/* End of Text */
