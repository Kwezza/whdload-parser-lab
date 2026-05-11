/* src_raw/filtering/selection_plan.c - Selection lane plan builder
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Builds a WhdSelectionPlan from a WhdBoundProfile by:
 *   1. Collecting all fields whose bucket_count > 1 (slash-enabled fields).
 *   2. Validating hard caps (field count, per-field bucket count, total lanes).
 *   3. Building lanes as the Cartesian product of the slash-enabled fields'
 *      bucket indices using iterative modulo/division indexing (no recursion).
 *
 * C89-compatible; vbcc-safe.
 * - All variables declared at the top of their enclosing block.
 * - No VLAs.
 * - No for-loop init declarations.
 * - No C99-only syntax.
 */

#include <filtering/selection_plan.h>
#include <filtering/tlv_filter.h>
#include <stdio.h>
#include <string.h>

/*========================================================================*/
/* Internal helpers                                                       */
/*========================================================================*/

/* Unsigned integer product with overflow detection.
 * Returns 0 and sets *overflow to 1 if the product would exceed limit,
 * otherwise returns a * b. */
static unsigned int safe_product(unsigned int a, unsigned int b,
                                 unsigned int limit, int *overflow)
{
    if (a == 0u || b == 0u) {
        return 0u;
    }
    if (a > limit / b) {
        *overflow = 1;
        return 0u;
    }
    return a * b;
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int whd_build_selection_plan(const WhdBoundProfile *profile,
                             WhdSelectionPlan      *plan_out)
{
    /* Indices of slash-enabled fields inside profile->fields[] */
    uint8_t      bucket_field_idx[FP_MAX_BUCKET_FIELDS];
    uint8_t      bucket_field_count;

    /* Per-slash-field bucket counts and strides for Cartesian indexing */
    unsigned int bucket_counts[FP_MAX_BUCKET_FIELDS];
    unsigned int strides[FP_MAX_BUCKET_FIELDS];

    unsigned int lane_count;
    int          overflow;
    uint8_t      i;
    unsigned int l;

    if (!profile || !plan_out) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    memset(plan_out, 0, sizeof(*plan_out));

    /* --- Step 1: collect slash-enabled fields ------------------------- */
    bucket_field_count = 0u;
    overflow           = 0;

    for (i = 0u; i < profile->field_count; i++) {
        const WhdBoundField *bf = &profile->fields[i];

        if (bf->bucket_count <= 1u) {
            continue; /* comma-only or empty — not a slash field */
        }

        /* Bucket count per field is validated during profile loading, but
         * defend here too for robustness. */
        if (bf->bucket_count > FP_MAX_BUCKETS_FIELD) {
            fprintf(stderr,
                    "selection_plan: [Filter.%s] has %u slash buckets"
                    " (max %d) -- profile rejected\n",
                    bf->field_name,
                    (unsigned)bf->bucket_count,
                    (int)FP_MAX_BUCKETS_FIELD);
            return WHD_FILTER_ERR_PROFILE_LOAD;
        }

        if (bucket_field_count >= FP_MAX_BUCKET_FIELDS) {
            fprintf(stderr,
                    "selection_plan: more than %d fields use slash buckets"
                    " -- profile rejected\n",
                    (int)FP_MAX_BUCKET_FIELDS);
            return WHD_FILTER_ERR_PROFILE_LOAD;
        }

        bucket_field_idx[bucket_field_count]   = i;
        bucket_counts[bucket_field_count]       = (unsigned int)bf->bucket_count;
        bucket_field_count++;
    }

    /* --- Step 2: validate lane count ---------------------------------- */
    if (bucket_field_count == 0u) {
        /* No slash fields: one implicit lane matching any variant. */
        plan_out->lane_count         = 1u;
        plan_out->bucket_field_count = 0u;
        plan_out->lanes[0].req_count = 0u;
        return WHD_FILTER_OK;
    }

    lane_count = 1u;
    for (i = 0u; i < bucket_field_count; i++) {
        lane_count = safe_product(lane_count, bucket_counts[i],
                                  FP_MAX_SELECTION_LANES, &overflow);
        if (overflow || lane_count > (unsigned int)FP_MAX_SELECTION_LANES) {
            fprintf(stderr,
                    "selection_plan: Cartesian product of slash buckets"
                    " exceeds %d lanes -- profile rejected\n",
                    (int)FP_MAX_SELECTION_LANES);
            return WHD_FILTER_ERR_PROFILE_LOAD;
        }
    }

    /* --- Step 3: compute strides for Cartesian indexing --------------- */
    /* stride[i] = product of bucket_counts[j] for j > i
     * For lane L and slash-field i: bucket_index = (L / stride[i]) % bucket_counts[i] */
    {
        unsigned int stride;
        int          j;
        stride = 1u;
        for (j = (int)bucket_field_count - 1; j >= 0; j--) {
            strides[j] = stride;
            stride *= bucket_counts[j];
        }
    }

    /* --- Step 4: build lanes ------------------------------------------ */
    plan_out->lane_count         = (uint8_t)lane_count;
    plan_out->bucket_field_count = bucket_field_count;

    for (l = 0u; l < lane_count; l++) {
        WhdSelectionLane *lane = &plan_out->lanes[l];
        lane->req_count = bucket_field_count;

        for (i = 0u; i < bucket_field_count; i++) {
            lane->reqs[i].field_index  = bucket_field_idx[i];
            lane->reqs[i].bucket_index =
                (uint8_t)((l / strides[i]) % bucket_counts[i]);
        }
    }

    return WHD_FILTER_OK;
}

/* End of Text */
