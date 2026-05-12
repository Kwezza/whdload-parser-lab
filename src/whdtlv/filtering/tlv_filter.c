/* src_raw/filtering/tlv_filter.c - Public API implementation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage I: full pipeline wired into whd_filter_run().
 *
 * Pipeline:
 *   load TLV runtime -> validate CSV CRCs -> load profile ->
 *   build variant views -> group by base name -> score & select ->
 *   write output file -> fill WhdFilterResult
 *
 * C89-compatible; vbcc-safe.
 */

#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_crc_validate.h"
#include "whdtlv/filtering/profile_binder.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/tlv_select.h"
#include "whdtlv/filtering/tlv_results.h"
#include "whdtlv/filtering/whd_search.h"
#include <string.h>

/*------------------------------------------------------------------------*/

int whd_filter_run(const WhdFilterRequest *request, WhdFilterResult *result)
{
    TlvRuntime           rt;
    WhdBoundProfile      profile;
    WhdVariantArray      arr;
    WhdGroupSet          gs;
    WhdSelectResult      sel;
    WhdGroupAllowList    allow;
    WhdCrcValidateResult crc_result;
    uint8_t              display_fid;
    int                  rc;
    int                  has_profile;
    int                  has_search;
    int                  arr_built;
    int                  gs_built;
    int                  sel_built;

    if (!request) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    if (result) {
        memset(result, 0, sizeof(*result));
    }

    arr_built   = 0;
    gs_built    = 0;
    sel_built   = 0;
    has_profile = 0;
    has_search  = 0;
    memset(&allow, 0, sizeof(allow));

    /* -- Load TLV runtime -------------------------------------------- */
    tlv_runtime_init(&rt);
    rc = tlv_runtime_load(&rt, request->tlv_path);
    if (rc != WHD_FILTER_OK) {
        return rc;
    }

    /* -- CRC validation ----------------------------------------------- */
    memset(&crc_result, 0, sizeof(crc_result));
    if (request->defs_dir) {
        rc = tlv_crc_validate(&rt, request->defs_dir, request->flags,
                              &crc_result);
        if (result) {
            result->crc_mismatch_count = crc_result.mismatch_count;
        }
        if (rc != WHD_FILTER_OK) {
            tlv_runtime_free(&rt);
            return rc;
        }
    }

    /* -- Profile load and bind --------------------------------------- */
    memset(&profile, 0, sizeof(profile));
    if (request->profile_path) {
        rc = whd_profile_load(request->profile_path, &rt,
                              request->defs_dir, &profile);
        if (rc != WHD_FILTER_OK) {
            tlv_runtime_free(&rt);
            return rc;
        }
        has_profile = 1;
        if (result && profile.had_warnings) {
            result->had_warnings = 1;
        }
    }
    (void)has_profile;

    /* -- Build variant views ----------------------------------------- */
    display_fid = tlv_runtime_field_id(&rt, "display_name");
    if (display_fid == 0) {
        tlv_runtime_free(&rt);
        return WHD_FILTER_ERR_TLV_NO_VARIANTS;
    }

    rc = tlv_variant_build(&arr,
                            rt.reader.buffer + rt.data_offset,
                            rt.reader.size   - rt.data_offset,
                            display_fid,
                            rt.group_id_field_id);
    if (rc != WHD_FILTER_OK) {
        tlv_runtime_free(&rt);
        return rc;
    }
    arr_built = 1;

    if (arr.count == 0u) {
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return WHD_FILTER_ERR_TLV_NO_VARIANTS;
    }

    if (result) {
        result->total_variants = arr.count;
    }

    /* -- Group variants ---------------------------------------------- */
    rc = tlv_group_build(&gs, &arr, (rt.group_id_field_id != 0u));
    if (rc != WHD_FILTER_OK) {
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return rc;
    }
    gs_built = 1;

    if (result) {
        result->total_groups = gs.group_count;
    }

    /* -- Search pre-filter ------------------------------------------- */
    if (request->search_pattern && request->search_pattern[0] != '\0') {
        WhdSearchRequest search_req;
        search_req.pattern = request->search_pattern;
        search_req.flags   = WHD_SEARCHF_ENABLED | WHD_SEARCHF_CASE_INSENSITIVE;
        rc = whd_search_build_group_allow_list(&rt, &gs, &search_req, &allow);
        if (rc != WHD_FILTER_OK) {
            tlv_group_free(&gs);
            tlv_variant_free(&arr);
            tlv_runtime_free(&rt);
            return rc;
        }
        has_search = 1;
        if (result) {
            result->search_matched_groups = allow.matched_count;
        }
    }

    /* -- Score and select -------------------------------------------- */
    rc = tlv_select_run(&sel, &gs,
                         &arr,
                         has_profile ? &profile : NULL,
                         has_search ? &allow : NULL);
    if (rc != WHD_FILTER_OK) {
        if (has_search) { whd_group_allow_list_free(&allow); }
        tlv_group_free(&gs);
        tlv_variant_free(&arr);
        tlv_runtime_free(&rt);
        return rc;
    }
    sel_built = 1;

    if (result) {
        result->selected_count           = sel.selected_count;
        result->total_selected_variants  = sel.total_selected_variants;
        result->lane_count               = sel.lane_count;
        result->rejected_variants_count  = sel.rejected_variants_count;
        result->rejected_groups_count    = sel.rejected_groups_count;
    }

    /* -- Write output file ------------------------------------------- */
    if (request->output_path) {
        rc = tlv_results_write_file(request->output_path, &sel, &gs, &arr);
        if (rc != WHD_FILTER_OK) {
            tlv_select_free(&sel);
            if (has_search) { whd_group_allow_list_free(&allow); }
            tlv_group_free(&gs);
            tlv_variant_free(&arr);
            tlv_runtime_free(&rt);
            return rc;
        }
    }

    /* -- Cleanup ----------------------------------------------------- */
    if (sel_built)  { tlv_select_free(&sel);           }
    if (has_search) { whd_group_allow_list_free(&allow); }
    if (gs_built)   { tlv_group_free(&gs);             }
    if (arr_built)  { tlv_variant_free(&arr);          }
    tlv_runtime_free(&rt);

    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

void whd_filter_free_results(WhdFilterOutputList *list)
{
    if (!list) {
        return;
    }
    list->entries = NULL;
    list->count   = 0;
}

/*------------------------------------------------------------------------*/

const char *whd_filter_error_string(int error_code)
{
    switch (error_code) {
    case WHD_FILTER_OK:                   return "ok";
    case WHD_FILTER_ERR_BAD_ARG:          return "bad argument";
    case WHD_FILTER_ERR_TLV_OPEN:         return "unable to open TLV file";
    case WHD_FILTER_ERR_TLV_HEADER:       return "invalid TLV header";
    case WHD_FILTER_ERR_TLV_VERSION:      return "unsupported TLV version";
    case WHD_FILTER_ERR_TLV_NO_VARIANTS:  return "no variants found in TLV";
    case WHD_FILTER_ERR_CSV_MISSING:      return "missing CSV file";
    case WHD_FILTER_ERR_CSV_UNREADABLE:   return "CSV file unreadable";
    case WHD_FILTER_ERR_CSV_CRC_MISMATCH: return "CSV CRC mismatch";
    case WHD_FILTER_ERR_PROFILE_LOAD:     return "unable to load profile";
    case WHD_FILTER_ERR_PROFILE_BIND:     return "profile could not be bound to TLV field map";
    case WHD_FILTER_ERR_NO_GROUPS:        return "no groups produced from TLV";
    case WHD_FILTER_ERR_OUTPUT_WRITE:     return "unable to write output file";
    case WHD_FILTER_ERR_OOM:              return "out of memory";
    default:                              return "unknown error";
    }
}

/* End of Text */
