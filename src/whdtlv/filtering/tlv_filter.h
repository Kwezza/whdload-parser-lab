/* filtering/tlv_filter.h - Public API for the TLV runtime filtering subsystem
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * This is the only header that host code and the filter_harness tool need
 * to include.  All implementation detail lives in the other modules under
 * src_raw/filtering/.
 *
 * Design constraints:
 *   - C89-compatible (no VLAs, no declarations inside for-loops)
 *   - Compatible with vbcc Amiga target
 *   - No direct printing from reusable code
 */

#ifndef FILTERING_TLV_FILTER_H
#define FILTERING_TLV_FILTER_H

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Error codes                                                            */

#define WHD_FILTER_OK                     0
#define WHD_FILTER_ERR_BAD_ARG           -1
#define WHD_FILTER_ERR_TLV_OPEN          -2
#define WHD_FILTER_ERR_TLV_HEADER        -3
#define WHD_FILTER_ERR_TLV_VERSION       -4
#define WHD_FILTER_ERR_TLV_NO_VARIANTS   -5
#define WHD_FILTER_ERR_CSV_MISSING       -6
#define WHD_FILTER_ERR_CSV_UNREADABLE    -7
#define WHD_FILTER_ERR_CSV_CRC_MISMATCH  -8
#define WHD_FILTER_ERR_PROFILE_LOAD      -9
#define WHD_FILTER_ERR_PROFILE_BIND      -10
#define WHD_FILTER_ERR_NO_GROUPS         -11
#define WHD_FILTER_ERR_OUTPUT_WRITE      -12
#define WHD_FILTER_ERR_OOM               -13

/*------------------------------------------------------------------------*/
/* Flags                                                                  */

#define WHD_FILTER_CRC_STRICT            0x0001u
#define WHD_FILTER_CRC_WARNONLY          0x0002u

/*------------------------------------------------------------------------*/
/* Request / result structs                                               */

typedef struct WhdFilterRequest {
    const char   *tlv_path;
    const char   *profile_path;
    const char   *defs_dir;
    const char   *pack_types_path;
    const char   *output_path;
    const char   *search_pattern; /* optional wildcard/substring; NULL = no search */
    unsigned int  flags;
} WhdFilterRequest;

typedef struct WhdFilterResult {
    unsigned long total_variants;
    unsigned long total_groups;
    unsigned long selected_count;           /* groups with >=1 lane satisfied */
    unsigned long total_selected_variants;  /* sum of per-group lane winners */
    unsigned long lane_count;               /* selection lanes used (>=1)    */
    unsigned long rejected_variants_count;  /* individual variants excluded by profile */
    unsigned long rejected_groups_count;    /* groups where every variant was excluded */
    unsigned long crc_mismatch_count;
    unsigned long search_matched_groups;    /* groups matched by search_pattern; 0 if no search */
    int           had_warnings;
} WhdFilterResult;

/*------------------------------------------------------------------------*/
/* In-memory result list (for future use)                                */

typedef struct WhdFilterSelectedEntry {
    const char    *filename;
    const char    *group_name;
    unsigned long  score;
    unsigned short variant_index;
    unsigned short flags;
} WhdFilterSelectedEntry;

typedef struct WhdFilterOutputList {
    WhdFilterSelectedEntry *entries;
    unsigned long           count;
} WhdFilterOutputList;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * Run the full filter pipeline described by request.
 * Returns WHD_FILTER_OK on success, a negative error code on failure.
 * result may be NULL if the caller does not need statistics.
 */
int whd_filter_run(const WhdFilterRequest *request, WhdFilterResult *result);

/* Free memory owned by an output list. */
void whd_filter_free_results(WhdFilterOutputList *list);

/* Return a human-readable description of an error code. */
const char *whd_filter_error_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_FILTER_H */
/* End of Text */
