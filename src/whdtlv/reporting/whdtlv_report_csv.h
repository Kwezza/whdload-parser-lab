/* src/whdtlv/reporting/whdtlv_report_csv.h - Host-side TLV CSV export API
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Reads a prebuilt .tlv file, resolves stored numeric field values back to
 * human-readable CSV tokens/descriptions where possible, and writes a CSV
 * file suitable for opening in Excel.
 *
 * Two output modes are supported:
 *   WIDE - one row per variant; multi-values joined with semicolons.
 *   LONG - one row per stored field value; value_index tracks repetitions.
 *
 * This subsystem is HOST-ONLY.  It must not be compiled into the Amiga
 * WHDFetch runtime.  The guard below enforces that.
 *
 * It has no dependency on profile loading or scoring.  It shares the
 * lower-level TLV parsing layer (tlv_runtime, tlv_variant, tlv_group,
 * csv_cache) with the filtering subsystem but is otherwise independent.
 */

#ifndef WHDTLV_REPORT_CSV_H
#define WHDTLV_REPORT_CSV_H

#ifdef PLATFORM_AMIGA
#error "whdtlv_report_csv.h is host-only and must not be compiled for Amiga targets."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Error codes                                                            */

#define WHDTLV_REPORT_OK              0   /* success                      */
#define WHDTLV_REPORT_ERR_BAD_ARG   -1   /* NULL or invalid argument      */
#define WHDTLV_REPORT_ERR_TLV_OPEN  -2   /* TLV file could not be opened  */
#define WHDTLV_REPORT_ERR_TLV_PARSE -3   /* TLV structure is invalid      */
#define WHDTLV_REPORT_ERR_CSV_OPEN  -4   /* output CSV could not be opened*/
#define WHDTLV_REPORT_ERR_OOM       -5   /* allocation failure            */

/*------------------------------------------------------------------------*/
/* Output mode                                                            */

typedef enum WhdTlvReportCsvMode {
    WHDTLV_REPORT_CSV_WIDE = 0, /* one row per variant (default)         */
    WHDTLV_REPORT_CSV_LONG = 1  /* one row per stored field value        */
} WhdTlvReportCsvMode;

/*------------------------------------------------------------------------*/
/* Options                                                                */

typedef struct WhdTlvReportOptions {
    WhdTlvReportCsvMode mode;         /* WIDE or LONG                     */
    int include_ids;                  /* add raw numeric ID columns       */
    int include_descriptions;         /* add CSV description columns      */
    int include_status;               /* add resolution/debug status cols */
    int only_multi_variant_groups;    /* skip single-variant groups       */
    int only_problem_rows;            /* skip fully-resolved rows         */
    int include_effective;            /* add _effective companion columns */
    unsigned int reserved[7];         /* zero-initialise; ABI padding     */
} WhdTlvReportOptions;

/*------------------------------------------------------------------------*/
/* Summary counters                                                       */

typedef struct WhdTlvReportSummary {
    unsigned long variants_total;          /* variants scanned             */
    unsigned long groups_total;            /* groups scanned               */
    unsigned long rows_written;            /* CSV data rows written        */
    unsigned long fields_written;          /* field values written         */
    unsigned long values_resolved;         /* resolved to CSV token        */
    unsigned long values_unresolved;       /* could not resolve            */
    unsigned long problem_rows;            /* rows with non-ok status      */
    unsigned long multi_value_fields_seen; /* fields seen more than once   */
    unsigned long multi_variant_groups_seen; /* groups with >1 variant     */
    unsigned long effective_explicit;      /* fields resolved from TLV     */
    unsigned long effective_default;       /* fields resolved from CSV def */
    unsigned long effective_invalid_default; /* invalid default rows seen  */
    unsigned int  reserved[5];             /* zero-initialise; ABI padding */
} WhdTlvReportSummary;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * whdtlv_report_options_defaults - fill *opts with safe default values.
 *
 * Call this before customising options to ensure all reserved fields are
 * zeroed and the mode defaults to WIDE.
 */
void whdtlv_report_options_defaults(WhdTlvReportOptions *opts);

/*
 * whdtlv_report_csv_file - export a TLV file to CSV.
 *
 *   tlv_path        - path to the input .tlv file (required)
 *   defs_dir        - path to the CSV definitions directory, e.g.
 *                     "assets_raw/defs" (required)
 *   output_csv_path - path for the output .csv file (required)
 *   options         - export options; if NULL, defaults are used
 *   summary         - filled with export statistics on success; may be NULL
 *
 * Returns WHDTLV_REPORT_OK on success, or a negative error code.
 *
 * Non-fatal conditions (missing CSV files, unresolvable IDs) do not cause
 * failure; they are reflected in the summary counters and as status strings
 * in the output.
 *
 * Fatal conditions: missing required arguments, unreadable TLV, invalid TLV
 * structure, output write failure, or allocation failure.
 */
int whdtlv_report_csv_file(
    const char                *tlv_path,
    const char                *defs_dir,
    const char                *output_csv_path,
    const WhdTlvReportOptions *options,
    WhdTlvReportSummary       *summary
);

#ifdef __cplusplus
}
#endif

#endif /* WHDTLV_REPORT_CSV_H */
/* End of Text */
