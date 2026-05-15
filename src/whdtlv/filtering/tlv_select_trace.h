/* src/whdtlv/filtering/tlv_select_trace.h - Selection trace types and collector
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Compile-guarded trace infrastructure for tlv_select_run_traced().
 * Enabled only when WHDTLV_ENABLE_SELECTION_TRACE is defined and non-zero.
 *
 * The trace records every selection decision (exclude rejection, lane
 * eligibility, duplicate suppression, winner, loser) for post-processing
 * by the host-side profile report writer.
 *
 * Not compiled into the normal WHDFetch / Amiga build.
 *
 * C89-compatible; vbcc-safe.
 */

#ifndef FILTERING_TLV_SELECT_TRACE_H
#define FILTERING_TLV_SELECT_TRACE_H

#if WHDTLV_ENABLE_SELECTION_TRACE

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Reason codes for trace rows                                            */

typedef enum WhdTlvTraceReason {
    WHDTLV_TRACE_REASON_WINNER = 0,          /* selected by a lane                 */
    WHDTLV_TRACE_REASON_LOST_SCORE,          /* eligible but outscored             */
    WHDTLV_TRACE_REASON_REJECTED_EXCLUDE,    /* excluded by profile exclude rule   */
    WHDTLV_TRACE_REASON_NOT_LANE_ELIGIBLE,   /* failed lane bucket requirement     */
    WHDTLV_TRACE_REASON_DUPLICATE_SUPPRESSED,/* already selected for earlier lane  */
    WHDTLV_TRACE_REASON_SEARCH_GROUP_SKIPPED,/* group did not match search pattern */
    WHDTLV_TRACE_REASON_NO_SCORE,            /* eligible but no lane winner found  */
    WHDTLV_TRACE_REASON_UNKNOWN              /* unclassified (should not occur)    */
} WhdTlvTraceReason;

/*------------------------------------------------------------------------*/
/* One trace record emitted per decision                                  */

typedef struct WhdTlvSelectionTraceRow {
    unsigned long group_index;           /* index into WhdGroupSet.groups[]        */
    unsigned long variant_index;         /* index into WhdVariantArray.items[]     */
    unsigned long lane_index;            /* 0xFFFFFFFFul = not lane-specific        */
    unsigned short group_id;             /* group_id from TLV; 0 if unknown        */

    int selected;                        /* 1 = winner for this lane               */
    int rejected;                        /* 1 = excluded by profile                */
    int eligible;                        /* 1 = passed lane requirements           */
    int duplicate_suppressed;            /* 1 = already selected in earlier lane   */

    unsigned long score_total;           /* score from score_variant_for_lane()    */
    unsigned long lost_to_score;         /* winner score (only for LOST_SCORE)     */
    unsigned long lost_to_variant_index; /* winner arr_idx; 0xFFFFFFFFul if N/A   */

    unsigned char reject_field_index;    /* profile field index causing rejection;
                                            0xFF = not applicable                  */

    WhdTlvTraceReason reason;
} WhdTlvSelectionTraceRow;

/*------------------------------------------------------------------------*/
/* Collector - growable array of trace rows                               */

typedef struct WhdTlvSelectionTrace {
    WhdTlvSelectionTraceRow *rows;
    unsigned long            count;
    unsigned long            capacity;
} WhdTlvSelectionTrace;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Initialise a trace collector.  Call before passing to tlv_select_run_traced(). */
void whdtlv_trace_init(WhdTlvSelectionTrace *trace);

/* Release memory owned by trace.  Does not free trace itself. */
void whdtlv_trace_free(WhdTlvSelectionTrace *trace);

/* Append one row to the collector.
 * Returns 0 on success, -1 on allocation failure. */
int whdtlv_trace_add_row(WhdTlvSelectionTrace          *trace,
                          const WhdTlvSelectionTraceRow *row);

#ifdef __cplusplus
}
#endif

#endif /* WHDTLV_ENABLE_SELECTION_TRACE */

#endif /* FILTERING_TLV_SELECT_TRACE_H */
/* End of Text */
