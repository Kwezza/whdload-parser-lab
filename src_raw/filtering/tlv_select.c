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

#include <filtering/tlv_select.h>
#include <filtering/tlv_filter.h>
#include <filtering/whd_search.h>
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

int tlv_select_run(WhdSelectResult             *out,
                   const WhdGroupSet           *gs,
                   const WhdVariantArray       *arr,
                   const WhdBoundProfile       *profile,
                   const WhdGroupAllowList     *allow)
{
    WhdSelectEntry *entries;
    unsigned long   g;

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

    entries = (WhdSelectEntry *)malloc(gs->group_count * sizeof(WhdSelectEntry));
    if (!entries) {
        return WHD_FILTER_ERR_OOM;
    }

    out->entries = entries;
    out->count   = gs->group_count;

    for (g = 0u; g < gs->group_count; g++) {
        const WhdVariantGroup *grp    = &gs->groups[g];
        WhdSelectEntry        *entry  = &entries[g];
        unsigned long          best_score    = 0u;
        int                    any_accepted  = 0;
        unsigned long          vi;

        entry->variant_index = WHD_NO_SELECTION;
        entry->score         = 0u;
        entry->all_rejected  = 0;

        /* Search pre-filter: skip groups not in the allow list.           */
        if (!whd_group_allowed(allow, g)) {
            continue;
        }

        for (vi = 0u; vi < grp->variant_count; vi++) {
            unsigned long         arr_idx = gs->sorted_indices[grp->first_variant + vi];
            const WhdVariantView *v       = &arr->items[arr_idx];
            WhdVariantScore       vs;

            tlv_select_score_variant(&vs, v, profile);

            if (vs.rejected) {
                out->rejected_variants_count++;
                continue;
            }

            any_accepted = 1;

            /* First-encountered wins on tie (sorted_indices preserves TLV
             * order within a group via stable sort, but qsort is not stable.
             * We simply take strict > to keep first-encountered on equal score.) */
            if (entry->variant_index == WHD_NO_SELECTION || vs.score > best_score) {
                entry->variant_index = arr_idx;
                entry->score         = vs.score;
                best_score           = vs.score;
            }
        }

        if (!any_accepted) {
            entry->all_rejected = 1;
            out->rejected_groups_count++;
        } else {
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
    out->count                    = 0;
    out->selected_count           = 0;
    out->rejected_variants_count  = 0;
    out->rejected_groups_count    = 0;
}

/* End of Text */
