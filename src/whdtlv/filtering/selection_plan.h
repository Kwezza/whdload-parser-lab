/* filtering/selection_plan.h - Selection lane plan builder
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Builds a compact, fixed-size selection plan from a bound profile.
 *
 * When a profile field's include list contains "/" separators it defines
 * multiple selection buckets.  Multiple such fields combine as AND
 * requirements: the lane count is the Cartesian product of each slash-
 * enabled field's bucket count.
 *
 * For a profile with:
 *   [Filter.chipset]   include=AGA/ECS,OCS        (2 buckets)
 *   [Filter.special]   include=EarlyBuild/HiRes,LoRes/RemasteredEdition (3 buckets)
 *
 * whd_build_selection_plan() generates 2 * 3 = 6 lanes:
 *   lane 0: chipset bucket 0 (AGA)        AND special bucket 0 (EarlyBuild)
 *   lane 1: chipset bucket 0 (AGA)        AND special bucket 1 (HiRes,LoRes)
 *   lane 2: chipset bucket 0 (AGA)        AND special bucket 2 (RemasteredEdition)
 *   lane 3: chipset bucket 1 (ECS,OCS)   AND special bucket 0 (EarlyBuild)
 *   lane 4: chipset bucket 1 (ECS,OCS)   AND special bucket 1 (HiRes,LoRes)
 *   lane 5: chipset bucket 1 (ECS,OCS)   AND special bucket 2 (RemasteredEdition)
 *
 * Profiles with no slash fields produce one implicit lane with zero
 * requirements (matching any variant, preserving single-winner behaviour).
 *
 * Hard cap violations (too many bucket fields, too many buckets per field,
 * lane count overflow) return WHD_FILTER_ERR_PROFILE_LOAD with an
 * error message written to stderr.
 *
 * C89-compatible; vbcc-safe.
 */

#ifndef FILTERING_SELECTION_PLAN_H
#define FILTERING_SELECTION_PLAN_H

#include "platform.h"
#include "whdtlv/filtering/profile_binder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* One AND-requirement within a lane                                      */

typedef struct WhdLaneRequirement {
    uint8_t field_index;   /* index into WhdBoundProfile.fields[]  */
    uint8_t bucket_index;  /* bucket within that field's buckets[] */
} WhdLaneRequirement;

/*------------------------------------------------------------------------*/
/* One selection lane                                                     */

typedef struct WhdSelectionLane {
    uint8_t            req_count;
    WhdLaneRequirement reqs[FP_MAX_BUCKET_FIELDS];
} WhdSelectionLane;

/*------------------------------------------------------------------------*/
/* Complete selection plan for one filter run                             */

typedef struct WhdSelectionPlan {
    uint8_t           lane_count;          /* total generated lanes        */
    uint8_t           bucket_field_count;  /* fields that used "/"         */
    WhdSelectionLane  lanes[FP_MAX_SELECTION_LANES];
} WhdSelectionPlan;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Build a selection plan from a bound profile.
 *
 *   profile  - bound profile produced by whd_profile_load()
 *   plan_out - caller-allocated WhdSelectionPlan to fill
 *
 * Returns WHD_FILTER_OK on success.
 * Returns WHD_FILTER_ERR_BAD_ARG if either pointer is NULL.
 * Returns WHD_FILTER_ERR_PROFILE_LOAD if any hard cap is exceeded;
 *   a diagnostic is written to stderr identifying the violation. */
int whd_build_selection_plan(const WhdBoundProfile *profile,
                             WhdSelectionPlan      *plan_out);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_SELECTION_PLAN_H */
/* End of Text */
