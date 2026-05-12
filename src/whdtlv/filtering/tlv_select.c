/* src_raw/filtering/tlv_select.c - Score variants and select best per group
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage H: real scoring engine.
 *
 * Scoring rules (from docs/profile_system.md):
 *   - exclude match rejects the whole variant immediately
 *   - field_score = (include_count - rank) * weight  where rank 0 = highest priority
 *   - empty include list (include_count == 0) accepts all but scores 0
 *   - token not in include list scores 0, does not reject
 *   - weight 0 means no score contribution; exclusions still apply
 *   - missing field uses CSV default token if defined
 *   - interior_fields added as unconditional bonus
 *   - first-encountered variant wins on tie (TLV order within group)
 *
 * Multi-value fields: a variant may carry multiple entries with the same
 * field_id.  Any single excluded value rejects the variant.  The best
 * (highest) field score among all values for a field is taken.
 *
 * Field values in TLV: 4-byte LE uint32 for CSV-backed fields.
 * rank_by_id[] is indexed by (id & 0xFF) - an intentional 8-bit hash that
 * matches the profile binder's storage convention.
 *
 * C89-compatible; vbcc-safe.
 */

#include "whdtlv/filtering/tlv_select.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/filtering/selection_plan.h"
#include "whdtlv/filtering/whd_search.h"
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Internal helpers                                                       */
/*========================================================================*/

/*------------------------------------------------------------------------*/
/* Read a 4-byte LE uint32 from a raw byte pointer.                      */

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*------------------------------------------------------------------------*/
/* Score a single (field, token-id) pair.
 * Returns 1 if the token triggers an exclude, otherwise 0.
 * On no-exclude, adds the field contribution to *best_field_score if
 * it improves the best score seen for this field so far.              */

static int check_and_score(const WhdBoundField *bf,
                            uint16_t             tid,
                            unsigned long       *best_field_score)
{
    uint8_t k;
    /* Check exclude list */
    for (k = 0u; k < bf->exclude_count; k++) {
        if (bf->exclude_ids[k] == tid) {
            return 1; /* excluded */
        }
    }
    /* Score against include list */
    if (bf->include_count > 0u && bf->weight > 0u) {
        uint8_t rank = bf->rank_by_id[tid & 0xFFu];
        if (rank != 0xFFu) {
            unsigned long fs =
                (unsigned long)(bf->include_count - rank) *
                (unsigned long)bf->weight;
            if (fs > *best_field_score) {
                *best_field_score = fs;
            }
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Check whether token tid appears in a specific bucket of bound field bf */

static int token_in_bucket(const WhdBoundField *bf,
                            uint8_t              bucket_index,
                            uint16_t             tid)
{
    uint8_t k;
    uint8_t start;
    uint8_t count;

    if (bucket_index >= bf->bucket_count) {
        return 0;
    }
    start = bf->buckets[bucket_index].start;
    count = bf->buckets[bucket_index].count;
    for (k = 0u; k < count; k++) {
        if (bf->include_ids[start + k] == tid) {
            return 1;
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Check whether a variant has at least one effective token for field bf  */
/* that falls inside the given bucket.                                    */
/*                                                                        */
/* Multi-value fields: any single matching value is enough.               */
/* Default-token fallback: identical to the scorer — if the variant       */
/* carries no value for this field but the CSV defines a default, that    */
/* default is used (e.g. OCS default lets a chipset-less variant match    */
/* an OCS bucket).                                                        */

static int variant_field_in_bucket(const WhdVariantView *v,
                                   const WhdBoundField  *bf,
                                   uint8_t               bucket_index)
{
    int      found_value;
    uint16_t fvi;

    found_value = 0;

    for (fvi = 0u; fvi < v->field_count; fvi++) {
        uint32_t u32;
        uint16_t tid;

        if (v->fields[fvi].field_id != bf->tlv_field_id) {
            continue;
        }
        if (v->fields[fvi].length < 4u) {
            continue;
        }

        found_value = 1;
        u32 = read_u32_le(v->fields[fvi].value);
        tid = (uint16_t)(u32 & 0xFFFFu);

        if (token_in_bucket(bf, bucket_index, tid)) {
            return 1;
        }
    }

    /* Default-token fallback: same logic as tlv_select_score_variant(). */
    if (!found_value && bf->has_default) {
        if (token_in_bucket(bf, bucket_index, bf->default_token_id)) {
            return 1;
        }
    }

    return 0;
}

/*------------------------------------------------------------------------*/
/* Check whether variant v is eligible for a selection lane.              */
/*                                                                        */
/* For each AND-requirement in the lane the variant must have at least    */
/* one effective token for that field that falls inside the required       */
/* bucket.  A lane with req_count == 0 (the implicit single-lane case     */
/* produced when no field uses "/") always matches.                       */
/*                                                                        */
/* Uses integer token IDs only — no string comparison in the hot path.   */

static int variant_matches_lane(const WhdVariantView   *v,
                                const WhdBoundProfile  *profile,
                                const WhdSelectionLane *lane)
{
    uint8_t r;

    for (r = 0u; r < lane->req_count; r++) {
        uint8_t              fi = lane->reqs[r].field_index;
        uint8_t              bi = lane->reqs[r].bucket_index;
        const WhdBoundField *bf = &profile->fields[fi];

        if (!variant_field_in_bucket(v, bf, bi)) {
            return 0;
        }
    }
    return 1; /* all requirements satisfied (or zero requirements) */
}

/*------------------------------------------------------------------------*/
/* Return the 0-based rank of tid within a specific bucket of bf.        */
/* Returns 0xFF if the token is not in that bucket.                      */

static uint8_t bucket_local_rank(const WhdBoundField *bf,
                                  uint8_t              bucket_index,
                                  uint16_t             tid)
{
    uint8_t k;
    uint8_t start;
    uint8_t count;

    if (bucket_index >= bf->bucket_count) {
        return 0xFFu;
    }
    start = bf->buckets[bucket_index].start;
    count = bf->buckets[bucket_index].count;
    for (k = 0u; k < count; k++) {
        if (bf->include_ids[start + k] == tid) {
            return k;
        }
    }
    return 0xFFu;
}

/*------------------------------------------------------------------------*/
/* Score a variant for a specific selection lane.                        */
/*                                                                        */
/* For fields that have a requirement in this lane (slash-enabled fields) */
/* bucket-local rank is used: the token's position within its bucket,    */
/* so ECS in bucket[1] scores as rank 0 rather than rank 1.             */
/*                                                                        */
/* Fields without a lane requirement are scored using global ranks as    */
/* normal.  Exclude checks are performed identically to the global scorer.*/
/*                                                                        */
/* On exclude match, out->rejected is set to 1 and out->score is 0.     */

static void score_variant_for_lane(WhdVariantScore        *out,
                                    const WhdVariantView   *v,
                                    const WhdBoundProfile  *profile,
                                    const WhdSelectionLane *lane)
{
    uint8_t fi;

    out->score        = 0u;
    out->rejected     = 0;
    out->reject_field = 0xFFu;

    if (!v) {
        return;
    }
    if (!profile) {
        out->score = (unsigned long)v->interior_fields;
        return;
    }

    for (fi = 0u; fi < profile->field_count; fi++) {
        const WhdBoundField *bf             = &profile->fields[fi];
        unsigned long        best_field_score = 0u;
        int                  found_value      = 0;
        int                  field_rejected   = 0;
        uint8_t              lane_bucket;
        uint16_t             fvi;

        /* Determine if this field has a requirement in this lane.        */
        /* If so, we'll use bucket-local rank for scoring.               */
        lane_bucket = 0xFFu;
        {
            uint8_t r;
            for (r = 0u; r < lane->req_count; r++) {
                if (lane->reqs[r].field_index == fi) {
                    lane_bucket = lane->reqs[r].bucket_index;
                    break;
                }
            }
        }

        /* Scan variant entries for this field_id */
        for (fvi = 0u; fvi < v->field_count && !field_rejected; fvi++) {
            uint32_t      u32;
            uint16_t      tid;
            uint8_t       ex_k;
            unsigned long fs;
            uint8_t       blr;

            if (v->fields[fvi].field_id != bf->tlv_field_id) {
                continue;
            }
            if (v->fields[fvi].length < 4u) {
                continue;
            }

            found_value = 1;
            u32 = read_u32_le(v->fields[fvi].value);
            tid = (uint16_t)(u32 & 0xFFFFu);

            /* Exclude check */
            for (ex_k = 0u; ex_k < bf->exclude_count; ex_k++) {
                if (bf->exclude_ids[ex_k] == tid) {
                    field_rejected = 1;
                    break;
                }
            }
            if (field_rejected) {
                break;
            }

            /* Score */
            if (bf->include_count > 0u && bf->weight > 0u) {
                if (lane_bucket != 0xFFu) {
                    /* Bucket-local: rank within this lane's bucket */
                    blr = bucket_local_rank(bf, lane_bucket, tid);
                    if (blr != 0xFFu) {
                        fs = (unsigned long)(bf->buckets[lane_bucket].count - blr)
                           * (unsigned long)bf->weight;
                        if (fs > best_field_score) {
                            best_field_score = fs;
                        }
                    }
                } else {
                    /* Global rank for non-slash fields */
                    uint8_t rank = bf->rank_by_id[tid & 0xFFu];
                    if (rank != 0xFFu) {
                        fs = (unsigned long)(bf->include_count - rank)
                           * (unsigned long)bf->weight;
                        if (fs > best_field_score) {
                            best_field_score = fs;
                        }
                    }
                }
            }
        }

        if (field_rejected) {
            out->rejected     = 1;
            out->reject_field = fi;
            out->score        = 0u;
            return;
        }

        /* Default-token fallback when variant has no value for this field */
        if (!found_value && bf->has_default) {
            uint16_t      dtid  = bf->default_token_id;
            unsigned long fs    = 0u;
            uint8_t       ex_k;
            uint8_t       blr;

            for (ex_k = 0u; ex_k < bf->exclude_count; ex_k++) {
                if (bf->exclude_ids[ex_k] == dtid) {
                    out->rejected     = 1;
                    out->reject_field = fi;
                    out->score        = 0u;
                    return;
                }
            }
            if (bf->include_count > 0u && bf->weight > 0u) {
                if (lane_bucket != 0xFFu) {
                    blr = bucket_local_rank(bf, lane_bucket, dtid);
                    if (blr != 0xFFu) {
                        fs = (unsigned long)(bf->buckets[lane_bucket].count - blr)
                           * (unsigned long)bf->weight;
                        if (fs > best_field_score) {
                            best_field_score = fs;
                        }
                    }
                } else {
                    uint8_t rank = bf->rank_by_id[dtid & 0xFFu];
                    if (rank != 0xFFu) {
                        fs = (unsigned long)(bf->include_count - rank)
                           * (unsigned long)bf->weight;
                        if (fs > best_field_score) {
                            best_field_score = fs;
                        }
                    }
                }
            }
        }

        out->score += best_field_score;
    }

    out->score += (unsigned long)v->interior_fields;
}

/*------------------------------------------------------------------------*/
/* Check whether arr_idx is already in the selected-for-this-group set.  */

static int is_already_selected(const unsigned long *sel,
                                uint8_t              count,
                                unsigned long        arr_idx)
{
    uint8_t k;
    for (k = 0u; k < count; k++) {
        if (sel[k] == arr_idx) {
            return 1;
        }
    }
    return 0;
}

/*========================================================================*/
/* Public: score a single variant                                         */
/*========================================================================*/

void tlv_select_score_variant(WhdVariantScore       *out,
                               const WhdVariantView  *v,
                               const WhdBoundProfile *profile)
{
    uint8_t fi;

    if (!out) {
        return;
    }
    out->score        = 0u;
    out->rejected     = 0;
    out->reject_field = 0xFFu;

    if (!v) {
        return;
    }

    if (!profile) {
        /* No profile: only the interior-fields bonus */
        out->score = (unsigned long)v->interior_fields;
        return;
    }

    for (fi = 0u; fi < profile->field_count; fi++) {
        const WhdBoundField *bf    = &profile->fields[fi];
        unsigned long  best_field_score = 0u;
        int            found_value      = 0;
        int            field_rejected   = 0;
        uint16_t       fvi;

        /* Scan all variant entries for this field_id */
        for (fvi = 0u; fvi < v->field_count && !field_rejected; fvi++) {
            uint32_t u32;
            uint16_t tid;

            if (v->fields[fvi].field_id != bf->tlv_field_id) {
                continue;
            }
            if (v->fields[fvi].length < 4u) {
                continue;
            }

            found_value = 1;
            u32 = read_u32_le(v->fields[fvi].value);
            tid = (uint16_t)(u32 & 0xFFFFu);

            if (check_and_score(bf, tid, &best_field_score)) {
                field_rejected = 1;
            }
        }

        if (field_rejected) {
            out->rejected     = 1;
            out->reject_field = fi;
            out->score        = 0u;
            return;
        }

        /* Default token when no value present for this field */
        if (!found_value && bf->has_default) {
            uint16_t dtid = bf->default_token_id;
            if (check_and_score(bf, dtid, &best_field_score)) {
                out->rejected     = 1;
                out->reject_field = fi;
                out->score        = 0u;
                return;
            }
        }

        out->score += best_field_score;
    }

    /* Interior-fields bonus (unconditional) */
    out->score += (unsigned long)v->interior_fields;
}

/*========================================================================*/
/* Public: score all groups and select best per group                     */
/*========================================================================*/

/*
 * Per-group rejection pre-pass cache limit.
 * Groups with more variants than this still work correctly; rejection
 * counting just stops at this boundary (overwhelmingly unlikely in practice).
 */
#define SEL_REJECT_CACHE 128

int tlv_select_run(WhdSelectResult             *out,
                   const WhdGroupSet           *gs,
                   const WhdVariantArray       *arr,
                   const WhdBoundProfile       *profile,
                   const WhdGroupAllowList     *allow)
{
    WhdSelectEntry   *entries;
    WhdSelectionPlan  plan;
    unsigned long     g;
    int               rc;

    if (!out) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (!gs || !arr) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    if (gs->group_count == 0u) {
        return WHD_FILTER_OK;
    }

    /* Build the selection plan from the bound profile.
     * NULL profile -> empty plan -> one implicit lane (all variants eligible). */
    if (profile) {
        rc = whd_build_selection_plan(profile, &plan);
        if (rc != WHD_FILTER_OK) {
            return rc;
        }
    } else {
        /* No profile: one implicit lane, no requirements. */
        memset(&plan, 0, sizeof(plan));
        plan.lane_count         = 1u;
        plan.bucket_field_count = 0u;
        plan.lanes[0].req_count = 0u;
    }

    entries = (WhdSelectEntry *)malloc(gs->group_count * sizeof(WhdSelectEntry));
    if (!entries) {
        return WHD_FILTER_ERR_OOM;
    }

    out->entries    = entries;
    out->count      = gs->group_count;
    out->lane_count = (unsigned long)plan.lane_count;

    for (g = 0u; g < gs->group_count; g++) {
        const WhdVariantGroup *grp   = &gs->groups[g];
        WhdSelectEntry        *entry = &entries[g];
        /* Per-group tracking of selected arr_idxs (duplicate suppression) */
        unsigned long          group_sel[FP_MAX_SELECTION_LANES];
        uint8_t                group_sel_count;
        /* Rejection pre-pass cache: 1 = rejected, 0 = not rejected        */
        uint8_t                reject_cache[SEL_REJECT_CACHE];
        unsigned long          cached_count;
        int                    any_accepted;
        unsigned long          lane_i;
        unsigned long          vi;
        uint8_t                li;

        /* Initialise entry */
        memset(entry->selected_indices, 0xFF, sizeof(entry->selected_indices));
        entry->lane_selected_count = 0u;
        entry->variant_index       = WHD_NO_SELECTION;
        entry->score               = 0u;
        entry->all_rejected        = 0;

        /* Search pre-filter: skip groups not in the allow list.           */
        if (!whd_group_allowed(allow, g)) {
            continue;
        }

        /* ---- Rejection pre-pass --------------------------------------- */
        /* Score each variant globally once to detect excludes.           */
        /* This avoids double-counting rejected variants across lanes.    */
        any_accepted  = 0;
        cached_count  = grp->variant_count;
        if (cached_count > SEL_REJECT_CACHE) {
            cached_count = SEL_REJECT_CACHE;
        }

        for (vi = 0u; vi < grp->variant_count; vi++) {
            unsigned long         arr_idx = gs->sorted_indices[grp->first_variant + vi];
            const WhdVariantView *v       = &arr->items[arr_idx];
            WhdVariantScore       vs;

            tlv_select_score_variant(&vs, v, profile);

            if (vs.rejected) {
                out->rejected_variants_count++;
                if (vi < (unsigned long)SEL_REJECT_CACHE) {
                    reject_cache[vi] = 1u;
                }
            } else {
                any_accepted = 1;
                if (vi < (unsigned long)SEL_REJECT_CACHE) {
                    reject_cache[vi] = 0u;
                }
            }
        }

        if (!any_accepted) {
            entry->all_rejected = 1;
            out->rejected_groups_count++;
            continue;
        }

        /* ---- Per-lane selection --------------------------------------- */
        group_sel_count = 0u;

        for (li = 0u; li < plan.lane_count; li++) {
            const WhdSelectionLane *lane       = &plan.lanes[li];
            unsigned long           best_idx   = WHD_NO_SELECTION;
            unsigned long           best_score = 0u;

            for (vi = 0u; vi < grp->variant_count; vi++) {
                unsigned long         arr_idx;
                const WhdVariantView *v;
                WhdVariantScore       vs;

                /* Check rejection cache (or re-score if beyond cache) */
                if (vi < (unsigned long)SEL_REJECT_CACHE) {
                    if (reject_cache[vi]) {
                        continue;
                    }
                } else {
                    WhdVariantScore vs2;
                    tlv_select_score_variant(
                        &vs2,
                        &arr->items[gs->sorted_indices[grp->first_variant + vi]],
                        profile);
                    if (vs2.rejected) {
                        continue;
                    }
                }

                arr_idx = gs->sorted_indices[grp->first_variant + vi];
                v       = &arr->items[arr_idx];

                /* Duplicate suppression: skip if already selected for
                 * this group in a previous lane.                        */
                if (is_already_selected(group_sel, group_sel_count, arr_idx)) {
                    continue;
                }

                /* Lane eligibility check */
                if (!variant_matches_lane(v, profile, lane)) {
                    continue;
                }

                /* Score using bucket-local rank for slash fields */
                score_variant_for_lane(&vs, v, profile, lane);
                if (vs.rejected) {
                    /* Exclude triggered during lane scoring (should be   */
                    /* rare; pre-pass should have caught most).           */
                    continue;
                }

                /* First-encountered wins on tie (strict >) */
                if (best_idx == WHD_NO_SELECTION || vs.score > best_score) {
                    best_idx   = arr_idx;
                    best_score = vs.score;
                }
            }

            if (best_idx != WHD_NO_SELECTION) {
                entry->selected_indices[entry->lane_selected_count] = best_idx;
                entry->lane_selected_count++;
                out->total_selected_variants++;

                /* Record for duplicate suppression in subsequent lanes  */
                if (group_sel_count < FP_MAX_SELECTION_LANES) {
                    group_sel[group_sel_count++] = best_idx;
                }
            }

            /* unused */
            (void)lane_i;
        }

        if (entry->lane_selected_count > 0u) {
            /* Backward-compat: first lane's winner is the canonical result */
            entry->variant_index = entry->selected_indices[0];
            /* Re-score the first winner globally for the summary score   */
            {
                WhdVariantScore vs;
                tlv_select_score_variant(
                    &vs, &arr->items[entry->variant_index], profile);
                entry->score = vs.score;
            }
            out->selected_count++;
        }
    }

    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

void tlv_select_free(WhdSelectResult *out)
{
    if (!out) {
        return;
    }
    free(out->entries);
    out->entries                  = NULL;
    out->count                    = 0u;
    out->selected_count           = 0u;
    out->total_selected_variants  = 0u;
    out->lane_count               = 0u;
    out->rejected_variants_count  = 0u;
    out->rejected_groups_count    = 0u;
}

/* End of Text */
