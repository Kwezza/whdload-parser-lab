/* src/whdtlv/reporting/whdtlv_report_profile.h - Profile-aware selection trace reporter
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Host-only.  Loads a .tlv file and a .profile file, runs the traced
 * selector, and writes a CSV that records every selection decision for
 * every variant in every group:
 *
 *   selected_marker  X=winner, -=lost/no-score, R=rejected,
 *                    N=not-lane-eligible, D=dup-suppressed
 *   selected_rank    1 for winner, 0 otherwise
 *   reason_code      human-readable reason string
 *   selection_lane   lane index (empty for rejected variants)
 *   lane_requirements requirements for that lane (e.g. "chipset=AGA")
 *   group_id         numeric group ID
 *   group_name       canonical group name
 *   display_name     variant filename (display_name field)
 *   score_total      score produced by the profile scorer
 *   reject_field     profile field that triggered exclusion
 *   lost_to_display_name  winner's filename (for losers)
 *   lost_to_score         winner's score (for losers)
 *   <field>               raw explicit token(s) from TLV, semicolon-joined
 *   <field>_effective     effective token (explicit or CSV default)
 *   <field>_effective_source  "explicit", "default", or "missing"
 *
 * Requires -DWHDTLV_ENABLE_SELECTION_TRACE=1 at compile time.
 * The Makefile adds this flag when building the report binary.
 */

#ifndef REPORTING_WHDTLV_REPORT_PROFILE_H
#define REPORTING_WHDTLV_REPORT_PROFILE_H

#ifdef PLATFORM_AMIGA
#  error "whdtlv_report_profile.h is host-only; do not include on Amiga"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Return codes                                                           */

#define WHDTLV_PROFILE_REPORT_OK               0
#define WHDTLV_PROFILE_REPORT_ERR_BAD_ARG    (-1)
#define WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN   (-2)
#define WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE  (-3)
#define WHDTLV_PROFILE_REPORT_ERR_CSV_OPEN   (-4)
#define WHDTLV_PROFILE_REPORT_ERR_OOM        (-5)
#define WHDTLV_PROFILE_REPORT_ERR_PROFILE    (-6)

/*------------------------------------------------------------------------*/
/* Options                                                                */

typedef struct WhdTlvProfileReportOptions {
    const char *tlv_path;        /* required: path to input .tlv file      */
    const char *defs_dir;        /* required: path to CSV definitions dir  */
    const char *profile_path;    /* required: path to .profile file        */
    const char *search_pattern;  /* optional: NULL or "" = all groups      */
    const char *output_csv_path; /* required: path to output .csv file     */
} WhdTlvProfileReportOptions;

/*------------------------------------------------------------------------*/
/* Summary counters (all optional — pass NULL to suppress)               */

typedef struct WhdTlvProfileReportSummary {
    unsigned long groups_total;    /* total groups in TLV                  */
    unsigned long variants_total;  /* total variants in TLV                */
    unsigned long rows_written;    /* CSV data rows written                */
    unsigned long winners;         /* rows with reason WINNER              */
    unsigned long losers;          /* rows with reason LOST_SCORE/NO_SCORE */
    unsigned long rejected;        /* rows with reason REJECTED_EXCLUDE    */
    unsigned long not_eligible;    /* rows with reason NOT_LANE_ELIGIBLE   */
    unsigned long dup_suppressed;  /* rows with reason DUPLICATE_SUPPRESSED*/
} WhdTlvProfileReportSummary;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * Run the profile-aware selection trace and write results to a CSV file.
 *
 * Returns WHDTLV_PROFILE_REPORT_OK on success, or one of the
 * WHDTLV_PROFILE_REPORT_ERR_* codes on failure.
 *
 * On failure the output file is closed and removed if it was already
 * opened.  On success the caller owns the completed output file.
 *
 * summary may be NULL.
 */
int whdtlv_report_profile_file(
    const WhdTlvProfileReportOptions *opts,
    WhdTlvProfileReportSummary       *summary);

#ifdef __cplusplus
}
#endif

#endif /* REPORTING_WHDTLV_REPORT_PROFILE_H */
/* End of Text */
