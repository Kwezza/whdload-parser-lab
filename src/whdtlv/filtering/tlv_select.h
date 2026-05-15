/* filtering/tlv_select.h - Score variants and select best per group
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Applies the bound profile to every variant in every group and picks
 * the highest-scoring non-excluded candidate.  Tie-break is
 * first-encountered wins (TLV order within the group).
 *
 * The selector does not print.  All output goes through tlv_results.h.
 *
 * Stage A: WhdBoundProfile is a placeholder stub.
 * Stage E: WhdBoundProfile is replaced with the real binding struct.
 */

#ifndef FILTERING_TLV_SELECT_H
#define FILTERING_TLV_SELECT_H

#include "platform.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/profile_binder.h"
#include "whdtlv/filtering/selection_plan.h"
#include "whdtlv/filtering/whd_search.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Sentinel for "no variant selected for this group"                     */

#define WHD_NO_SELECTION 0xFFFFFFFFul

/*------------------------------------------------------------------------*/
/* Per-group selection result                                             */

typedef struct WhdSelectEntry {
    unsigned long variant_index; /* backward-compat: first lane's winner (selected_indices[0]),
                                    or WHD_NO_SELECTION if no lane was satisfied              */
    unsigned long score;         /* score of the first-lane winner                            */
    int           all_rejected;  /* 1 if every variant was excluded by profile                */
    /* Multi-lane results (one entry per selection lane) */
    unsigned long selected_indices[FP_MAX_SELECTION_LANES]; /* WHD_NO_SELECTION = no winner    */
    uint8_t       lane_selected_count;                      /* lanes that produced a winner    */
} WhdSelectEntry;

typedef struct WhdSelectResult {
    WhdSelectEntry *entries;                 /* one entry per group                          */
    unsigned long   count;                   /* == gs->group_count                           */
    unsigned long   selected_count;          /* groups with at least one lane satisfied      */
    unsigned long   total_selected_variants; /* sum of lane_selected_count across all groups */
    unsigned long   lane_count;              /* lane count from the selection plan           */
    unsigned long   rejected_variants_count; /* unique variants excluded by profile          */
    unsigned long   rejected_groups_count;   /* groups where every variant was excluded      */
} WhdSelectResult;

/* WhdBoundProfile is now defined in profile_binder.h (Stage E). */

/*------------------------------------------------------------------------*/
/* Per-variant scoring detail (for debug/harness use)                    */

typedef struct WhdVariantScore {
    unsigned long score;
    int           rejected;
    uint8_t       reject_field; /* profile field index that triggered rejection;
                                   0xFF if variant was not rejected             */
} WhdVariantScore;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Score all groups using the bound profile and fill out.
 *
 * allow: optional search pre-filter allow list built by
 *   whd_search_build_group_allow_list().  Pass NULL to consider all groups.
 *   Groups not allowed are skipped; their entry gets WHD_NO_SELECTION and
 *   are not counted in selected_count or rejected_groups_count.
 *
 * Returns WHD_FILTER_OK or a negative error code. */
int tlv_select_run(WhdSelectResult             *out,
                   const WhdGroupSet           *gs,
                   const WhdVariantArray       *arr,
                   const WhdBoundProfile       *profile,
                   const WhdGroupAllowList     *allow);

/* Score a single variant and fill out.  Does not allocate.
 * Useful for per-variant debug output in the harness. */
void tlv_select_score_variant(WhdVariantScore       *out,
                               const WhdVariantView  *v,
                               const WhdBoundProfile *profile);

/* Release memory owned by out.  Does not free out itself. */
void tlv_select_free(WhdSelectResult *out);

/*------------------------------------------------------------------------*/
/* Optional trace API (host/reporting-only)                               */

#if WHDTLV_ENABLE_SELECTION_TRACE
#include "whdtlv/filtering/tlv_select_trace.h"

/* Like tlv_select_run() but also fills *trace with per-variant decision
 * records (rejections, lane eligibility, dup-suppression, winners, losers).
 * trace must be initialised with whdtlv_trace_init() before calling.
 * Selection results are identical to tlv_select_run() for the same inputs.
 * Returns WHD_FILTER_OK or a negative error code. */
int tlv_select_run_traced(WhdSelectResult             *out,
                           const WhdGroupSet           *gs,
                           const WhdVariantArray       *arr,
                           const WhdBoundProfile       *profile,
                           const WhdGroupAllowList     *allow,
                           WhdTlvSelectionTrace        *trace);
#endif /* WHDTLV_ENABLE_SELECTION_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_SELECT_H */
/* End of Text */
