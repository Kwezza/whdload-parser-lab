/* src/whdtlv/whdtlv_filter_facade.c - Public filtering facade
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implements the public filtering API declared in include/whdtlv/whdtlv.h:
 *
 *   whdtlv_filter_options_defaults()
 *   whdtlv_filter_to_list()
 *   whdtlv_filter_to_file()
 *   whdtlv_string_list_free()
 *
 * The pipeline mirrors whd_filter_run() in tlv_filter.c but:
 *   - accepts the public WhdTlvFilterOptions / WhdTlvFilterSummary types
 *   - calls tlv_results_collect_list() instead of tlv_results_write_file()
 *   - returns WHDTLV_OK (0) on success; WHDTLV_FILTER_ERR_* (<0) on failure
 *
 * No internal structs are exposed to the caller.  The caller only needs:
 *   #include "whdtlv/whdtlv.h"
 *
 * C89-compatible; vbcc-safe.
 */

#include "whdtlv/whdtlv.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_crc_validate.h"
#include "whdtlv/filtering/profile_binder.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/tlv_select.h"
#include "whdtlv/filtering/tlv_results.h"
#include "whdtlv/filtering/whd_search.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*========================================================================*/
/* Internal: map WHD_FILTER_ERR_* -> WHDTLV_FILTER_ERR_*                */
/*========================================================================*/

static int whd_filter_err_to_public(int internal_rc)
{
    switch (internal_rc) {
    case WHD_FILTER_OK:                 return WHDTLV_OK;
    case WHD_FILTER_ERR_BAD_ARG:        return WHDTLV_FILTER_ERR_INVALID_ARG;
    case WHD_FILTER_ERR_TLV_OPEN:       return WHDTLV_FILTER_ERR_IO;
    case WHD_FILTER_ERR_OUTPUT_WRITE:   return WHDTLV_FILTER_ERR_IO;
    case WHD_FILTER_ERR_TLV_HEADER:     return WHDTLV_FILTER_ERR_PARSE;
    case WHD_FILTER_ERR_TLV_VERSION:    return WHDTLV_FILTER_ERR_PARSE;
    case WHD_FILTER_ERR_TLV_NO_VARIANTS: return WHDTLV_FILTER_ERR_EMPTY;
    case WHD_FILTER_ERR_NO_GROUPS:      return WHDTLV_FILTER_ERR_EMPTY;
    case WHD_FILTER_ERR_CSV_MISSING:    return WHDTLV_FILTER_ERR_CRC;
    case WHD_FILTER_ERR_CSV_UNREADABLE: return WHDTLV_FILTER_ERR_CRC;
    case WHD_FILTER_ERR_CSV_CRC_MISMATCH: return WHDTLV_FILTER_ERR_CRC;
    case WHD_FILTER_ERR_PROFILE_LOAD:   return WHDTLV_FILTER_ERR_PROFILE;
    case WHD_FILTER_ERR_PROFILE_BIND:   return WHDTLV_FILTER_ERR_PROFILE;
    case WHD_FILTER_ERR_OOM:            return WHDTLV_FILTER_ERR_ALLOC;
    default:                            return WHDTLV_FILTER_ERR_INVALID_ARG;
    }
}

/*========================================================================*/
/* whdtlv_filter_options_defaults                                         */
/*========================================================================*/

void whdtlv_filter_options_defaults(WhdTlvFilterOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->enable_logging = 0;
    opts->strict_crc     = 1;
}

/*========================================================================*/
/* whdtlv_string_list_free                                                */
/*========================================================================*/

void whdtlv_string_list_free(WhdTlvStringList *list)
{
    /*
     * Safety invariant — safe to call in all four states:
     *
     * 1. Zero-init / NULL-reserved:
     *      memset(&list, 0, ...) or a result struct that was never populated.
     *      reserved == NULL, so the free branch is skipped.  No-op.
     *
     * 2. Empty success (count == 0, items == NULL, reserved == NULL):
     *      tlv_results_collect_list() sets reserved = NULL at entry then
     *      returns early without allocating when there are no matches.
     *      Same as case 1.
     *
     * 3. Full success (count > 0, items != NULL, reserved == block):
     *      items points into the reserved block (pointer table is at the
     *      start of the single malloc'd region; string data follows it).
     *      free(reserved) releases the entire region in one call.
     *      items and reserved are then set to NULL; no dangling pointers.
     *
     * 4. Failure / partial pipeline state:
     *      whdtlv_filter_to_list() initialises the struct to safe-empty
     *      before calling tlv_results_collect_list().  The allocator does
     *      one malloc; if that fails, reserved stays NULL (case 1).
     *      No intermediate partial allocation is possible.
     *
     * Consequence: the caller may always call whdtlv_string_list_free()
     * regardless of how the list was reached, including on failure returns
     * from whdtlv_filter_to_list() (though the doc says this is not
     * required — it is simply harmless).
     */
    if (!list) {
        return;
    }
    if (list->reserved) {
        free(list->reserved);
    }
    list->count    = 0u;
    list->items    = NULL;
    list->reserved = NULL;
}

/*========================================================================*/
/* whdtlv_filter_to_list                                                  */
/*========================================================================*/

int whdtlv_filter_to_list(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvStringList           *results,
    WhdTlvFilterSummary        *summary)
{
    WhdTlvFilterOptions  default_opts;
    TlvRuntime           rt;
    WhdBoundProfile      profile;
    WhdVariantArray      arr;
    WhdGroupSet          gs;
    WhdSelectResult      sel;
    WhdGroupAllowList    allow;
    WhdCrcValidateResult crc_result;
    uint8_t              display_fid;
    unsigned int         crc_flags;
    int                  rc;
    int                  has_profile;
    int                  has_search;
    int                  arr_built;
    int                  gs_built;
    int                  sel_built;

    /* -- Validate required arguments --------------------------------- */
    if (!tlv_path || !defs_dir || !results) {
        return WHDTLV_FILTER_ERR_INVALID_ARG;
    }

    /* -- Resolve options --------------------------------------------- */
    if (!options) {
        whdtlv_filter_options_defaults(&default_opts);
        options = &default_opts;
    }

    /* -- Initialise output to safe empty state ----------------------- */
    results->count    = 0u;
    results->items    = NULL;
    results->reserved = NULL;

    if (summary) {
        memset(summary, 0, sizeof(*summary));
    }

    /* -- Setup ------------------------------------------------------- */
    arr_built   = 0;
    gs_built    = 0;
    sel_built   = 0;
    has_profile = 0;
    has_search  = 0;
    memset(&allow,      0, sizeof(allow));
    memset(&crc_result, 0, sizeof(crc_result));

    crc_flags = options->strict_crc ? WHD_FILTER_CRC_STRICT
                                    : WHD_FILTER_CRC_WARNONLY;

    /* -- Load TLV runtime -------------------------------------------- */
    tlv_runtime_init(&rt);
    rc = tlv_runtime_load(&rt, tlv_path);
    if (rc != WHD_FILTER_OK) {
        return whd_filter_err_to_public(rc);
    }

    /* -- CRC validation ---------------------------------------------- */
    rc = tlv_crc_validate(&rt, defs_dir, crc_flags, &crc_result);
    if (summary) {
        summary->crc_files_checked = (unsigned int)(
            crc_result.ok_count        +
            crc_result.mismatch_count  +
            crc_result.missing_count   +
            crc_result.unreadable_count);
        summary->crc_mismatches = (unsigned int)crc_result.mismatch_count;
    }
    if (rc != WHD_FILTER_OK) {
        tlv_runtime_free(&rt);
        return whd_filter_err_to_public(rc);
    }

    /* -- Profile load and bind --------------------------------------- */
    memset(&profile, 0, sizeof(profile));
    if (profile_path && profile_path[0] != '\0') {
        rc = whd_profile_load(profile_path, &rt, defs_dir, &profile);
        if (rc != WHD_FILTER_OK) {
            tlv_runtime_free(&rt);
            return whd_filter_err_to_public(rc);
        }
        has_profile = 1;
    }

    /* -- Build variant views ----------------------------------------- */
    display_fid = tlv_runtime_field_id(&rt, "display_name");
    if (display_fid == 0u) {
        tlv_runtime_free(&rt);
        return WHDTLV_FILTER_ERR_EMPTY;
    }

    rc = tlv_variant_build(&arr,
                            rt.reader.buffer + rt.data_offset,
                            rt.reader.size   - rt.data_offset,
                            display_fid,
                            rt.group_id_field_id);
    if (rc != WHD_FILTER_OK) {
        tlv_runtime_free(&rt);
        return whd_filter_err_to_public(rc);
    }
    arr_built = 1;

    if (arr.count == 0u) {
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return WHDTLV_FILTER_ERR_EMPTY;
    }

    if (summary) {
        summary->variants_total = (unsigned int)arr.count;
    }

    /* -- Group variants ---------------------------------------------- */
    rc = tlv_group_build(&gs, &arr, (rt.group_id_field_id != 0u));
    if (rc != WHD_FILTER_OK) {
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return whd_filter_err_to_public(rc);
    }
    gs_built = 1;

    if (summary) {
        summary->groups_total = (unsigned int)gs.group_count;
    }

    /* -- Search pre-filter ------------------------------------------- */
    if (search_pattern && search_pattern[0] != '\0') {
        WhdSearchRequest search_req;
        search_req.pattern = search_pattern;
        search_req.flags   = WHD_SEARCHF_ENABLED | WHD_SEARCHF_CASE_INSENSITIVE;
        rc = whd_search_build_group_allow_list(&rt, &gs, &search_req, &allow);
        if (rc != WHD_FILTER_OK) {
            tlv_group_free(&gs);
            tlv_variant_free(&arr);
            tlv_runtime_free(&rt);
            return whd_filter_err_to_public(rc);
        }
        has_search = 1;
        if (summary) {
            summary->matched_groups = (unsigned int)allow.matched_count;
        }
    }

    /* If no search was applied, matched_groups mirrors groups_total so
     * callers can distinguish "no search" from "search matched nothing". */
    if (summary && !has_search) {
        summary->matched_groups = summary->groups_total;
    }

    /* -- Score and select -------------------------------------------- */
    rc = tlv_select_run(&sel, &gs,
                         &arr,
                         has_profile ? &profile : NULL,
                         has_search  ? &allow   : NULL);
    if (rc != WHD_FILTER_OK) {
        if (has_search) { whd_group_allow_list_free(&allow); }
        tlv_group_free(&gs);
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return whd_filter_err_to_public(rc);
    }
    sel_built = 1;

    if (summary) {
        summary->selected_variants = (unsigned int)sel.total_selected_variants;
        summary->selected_groups   = (unsigned int)sel.selected_count;
        summary->selection_lanes   = (unsigned int)sel.lane_count;
        summary->rejected_variants = (unsigned int)sel.rejected_variants_count;
        summary->rejected_groups   = (unsigned int)sel.rejected_groups_count;
    }

    /* -- Collect in-memory filename list ----------------------------- */
    rc = tlv_results_collect_list(&sel, &gs, &arr, results);
    if (rc != WHD_FILTER_OK) {
        if (sel_built)  { tlv_select_free(&sel);              }
        if (has_search) { whd_group_allow_list_free(&allow);  }
        if (gs_built)   { tlv_group_free(&gs);                }
        if (arr_built)  { tlv_variant_free(&arr);             }
        tlv_runtime_free(&rt);
        return whd_filter_err_to_public(rc);
    }

    /* -- Cleanup ----------------------------------------------------- */
    if (sel_built)  { tlv_select_free(&sel);              }
    if (has_search) { whd_group_allow_list_free(&allow);  }
    if (gs_built)   { tlv_group_free(&gs);                }
    if (arr_built)  { tlv_variant_free(&arr);             }
    tlv_runtime_free(&rt);

    return WHDTLV_OK;
}

/*========================================================================*/
/* whdtlv_filter_to_file                                                  */
/*========================================================================*/

int whdtlv_filter_to_file(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *output_list_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvFilterSummary        *summary)
{
    WhdTlvStringList list;
    FILE            *f;
    unsigned int     i;
    int              rc;

    if (!output_list_path) {
        return WHDTLV_FILTER_ERR_INVALID_ARG;
    }

    memset(&list, 0, sizeof(list));

    rc = whdtlv_filter_to_list(tlv_path, defs_dir, profile_path,
                                search_pattern, options, &list, summary);
    if (rc != WHDTLV_OK) {
        return rc;
    }

    f = fopen(output_list_path, "w");
    if (!f) {
        whdtlv_string_list_free(&list);
        return WHDTLV_FILTER_ERR_IO;
    }

    for (i = 0u; i < list.count; i++) {
        if (list.items[i]) {
            fprintf(f, "%s\n", list.items[i]);
        }
    }

    fclose(f);
    whdtlv_string_list_free(&list);
    return WHDTLV_OK;
}

/* End of Text */
