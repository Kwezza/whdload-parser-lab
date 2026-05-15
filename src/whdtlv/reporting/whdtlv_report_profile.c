/* src/whdtlv/reporting/whdtlv_report_profile.c - Profile-aware selection trace reporter
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Host-only.  Compile only with -DWHDTLV_ENABLE_SELECTION_TRACE=1.
 * The Makefile adds this flag when building the report binary.
 *
 * Pipeline:
 *   load TLV -> build variants/groups -> load profile -> build plan
 *   -> run traced selector -> walk trace rows -> write CSV
 */

#ifdef PLATFORM_AMIGA
#  error "whdtlv_report_profile.c is host-only"
#endif

#ifndef WHDTLV_ENABLE_SELECTION_TRACE
#  error "whdtlv_report_profile.c requires -DWHDTLV_ENABLE_SELECTION_TRACE=1"
#endif

#include "whdtlv/reporting/whdtlv_report_profile.h"

#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_reader.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/filtering/tlv_select.h"
#include "whdtlv/filtering/tlv_select_trace.h"
#include "whdtlv/filtering/profile_binder.h"
#include "whdtlv/filtering/selection_plan.h"
#include "whdtlv/filtering/whd_search.h"
#include "whdtlv/core/csv_cache.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Per-profile-field metadata for token resolution                        */
/*========================================================================*/

typedef struct {
    uint8_t tlv_field_id;
    char    field_name[32]; /* e.g. "chipset" */
    char    csv_name[64];   /* empty if not found in CRC map */
    int     csv_available;  /* 1 if the CSV was loaded successfully */
} ProfFieldInfo;

/*========================================================================*/
/* Runtime context                                                        */
/*========================================================================*/

typedef struct {
    TlvRuntime       rt;
    WhdVariantArray  arr;
    WhdGroupSet      gs;
    GlobalCSVManager mgr;
    uint8_t          display_fid;
    uint8_t          group_id_fid;
    int              arr_built;
    int              gs_built;
    int              mgr_init;
} ProfCtx;

/*========================================================================*/
/* Minimal string builder (avoids O(n^2) strlen in append loops)         */
/*========================================================================*/

typedef struct { char *buf; size_t pos; size_t cap; } SBuf;

static void sb_init(SBuf *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->pos = 0;
    sb->cap = cap;
    if (cap > 0) buf[0] = '\0';
}

static void sb_char(SBuf *sb, char c)
{
    if (sb->pos + 1 < sb->cap) {
        sb->buf[sb->pos++] = c;
        sb->buf[sb->pos]   = '\0';
    }
}

static void sb_str(SBuf *sb, const char *s)
{
    while (s && *s && sb->pos + 1 < sb->cap)
        sb->buf[sb->pos++] = *s++;
    if (sb->cap > 0) sb->buf[sb->pos] = '\0';
}

static void sb_uint(SBuf *sb, unsigned u)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", u);
    sb_str(sb, tmp);
}

/*========================================================================*/
/* CSV cell output                                                        */
/*========================================================================*/

/* Write a double-quoted CSV string cell.  Internal double-quotes are doubled.
 * Writes a trailing comma unless last == 1. */
static void write_str_cell(FILE *f, const char *s, int last)
{
    const char *p;
    fputc('"', f);
    if (s) {
        for (p = s; *p; ++p) {
            if (*p == '"') fputc('"', f); /* RFC 4180: escape " as "" */
            fputc(*p, f);
        }
    }
    fputc('"', f);
    if (!last) fputc(',', f);
}

/* Write an unquoted unsigned long cell with optional trailing comma. */
static void write_ulong_cell(FILE *f, unsigned long v, int last)
{
    fprintf(f, "%lu", v);
    if (!last) fputc(',', f);
}

/* Write an empty cell.  Writes only a comma (unless last). */
static void write_empty_cell(FILE *f, int last)
{
    if (!last) fputc(',', f);
}

/*========================================================================*/
/* TLV field value reader (matching the selector's format)               */
/*========================================================================*/

/* Field values are stored as 4-byte little-endian words.
 * The low 16 bits are the token ID. */
static uint32_t read_u32_le_local(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/*========================================================================*/
/* Case-insensitive ASCII compare (for CRC map lookup)                   */
/*========================================================================*/

static int ascii_icmp_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 1;
    }
    return 0;
}

/*========================================================================*/
/* CRC map CSV name lookup                                                */
/*========================================================================*/

/* Search the TLV CRC map for the CSV file name that corresponds to a
 * profile field name.  Returns the csv_name string owned by the CRC map
 * (do not free), or NULL if not found. */
static const char *find_csv_for_field(const TlvRuntime *rt,
                                       const char       *field_name)
{
    size_t        flen = strlen(field_name);
    unsigned long i;

    for (i = 0; i < rt->crc_map.count; ++i) {
        const char *cn = rt->crc_map.entries[i].csv_name;
        if (strlen(cn) == flen && ascii_icmp_n(cn, field_name, flen) == 0)
            return cn;
    }
    return NULL;
}

/*========================================================================*/
/* Per-field info table                                                   */
/*========================================================================*/

/* Populate per-field info and pre-load the CSVs so token resolution works. */
static void build_field_info(ProfFieldInfo       *info,
                              uint8_t              count,
                              const WhdBoundProfile *profile,
                              const TlvRuntime    *rt,
                              GlobalCSVManager    *mgr)
{
    uint8_t fi;

    for (fi = 0; fi < count; ++fi) {
        const WhdBoundField *bf  = &profile->fields[fi];
        const char          *csv = find_csv_for_field(rt, bf->field_name);

        info[fi].tlv_field_id = bf->tlv_field_id;
        strncpy(info[fi].field_name, bf->field_name,
                sizeof(info[fi].field_name) - 1);
        info[fi].field_name[sizeof(info[fi].field_name) - 1] = '\0';
        info[fi].csv_name[0]   = '\0';
        info[fi].csv_available = 0;

        if (csv) {
            strncpy(info[fi].csv_name, csv, sizeof(info[fi].csv_name) - 1);
            info[fi].csv_name[sizeof(info[fi].csv_name) - 1] = '\0';
            if (csv_cache_get_or_load(mgr, csv) != NULL)
                info[fi].csv_available = 1;
        }
    }
}

/*========================================================================*/
/* Token resolution                                                       */
/*========================================================================*/

/* Reverse-lookup a token ID to its short name string.  Returns NULL on
 * failure (not in CSV, CSV not loaded, or csv_name empty). */
static const char *resolve_token(GlobalCSVManager   *mgr,
                                  const ProfFieldInfo *pfi,
                                  uint16_t             tid)
{
    if (!pfi->csv_available || pfi->csv_name[0] == '\0')
        return NULL;
    return csv_cache_reverse_lookup(mgr, pfi->csv_name, (uint32_t)tid, false);
}

/*========================================================================*/
/* Lane requirements string builder                                       */
/*========================================================================*/

/* Build a human-readable string such as "chipset=AGA; language=En" that
 * describes the AND-requirements for a given selection lane.
 * lane_idx == 0xFFFFFFFFUL (not lane-specific) → empty string. */
static void build_lane_req_str(const WhdBoundProfile  *profile,
                                const WhdSelectionPlan *plan,
                                unsigned long           lane_idx,
                                GlobalCSVManager       *mgr,
                                const ProfFieldInfo    *finfo,
                                char *buf, size_t cap)
{
    const WhdSelectionLane *lane;
    SBuf   sb;
    uint8_t r, k;

    sb_init(&sb, buf, cap);

    if (lane_idx == 0xFFFFFFFFUL || lane_idx >= (unsigned long)plan->lane_count)
        return; /* leave empty */

    lane = &plan->lanes[(uint8_t)lane_idx];

    if (lane->req_count == 0) {
        sb_str(&sb, "single-lane");
        return;
    }

    for (r = 0; r < lane->req_count; ++r) {
        const WhdLaneRequirement *req   = &lane->reqs[r];
        const WhdBoundField      *bf    = &profile->fields[req->field_index];
        const ProfFieldInfo      *pfi   = &finfo[req->field_index];
        uint8_t                   start = bf->buckets[req->bucket_index].start;
        uint8_t                   count = bf->buckets[req->bucket_index].count;

        if (r > 0) { sb_char(&sb, ';'); sb_char(&sb, ' '); }
        sb_str(&sb, bf->field_name);
        sb_char(&sb, '=');

        for (k = 0; k < count; ++k) {
            uint16_t    tid = bf->include_ids[start + k];
            const char *tok = resolve_token(mgr, pfi, tid);
            char        tmp[16];

            if (k > 0) sb_char(&sb, ',');
            if (tok) {
                sb_str(&sb, tok);
            } else {
                sb_uint(&sb, (unsigned)tid);
            }
            (void)tmp; /* suppresses unused-variable warning with some compilers */
        }
    }
}

/*========================================================================*/
/* Reason → marker / code strings                                         */
/*========================================================================*/

static const char *reason_marker(WhdTlvTraceReason r)
{
    switch (r) {
    case WHDTLV_TRACE_REASON_WINNER:               return "X";
    case WHDTLV_TRACE_REASON_LOST_SCORE:           return "-";
    case WHDTLV_TRACE_REASON_NO_SCORE:             return "-";
    case WHDTLV_TRACE_REASON_REJECTED_EXCLUDE:     return "R";
    case WHDTLV_TRACE_REASON_NOT_LANE_ELIGIBLE:    return "N";
    case WHDTLV_TRACE_REASON_DUPLICATE_SUPPRESSED: return "D";
    case WHDTLV_TRACE_REASON_SEARCH_GROUP_SKIPPED: return "S";
    default:                                        return "?";
    }
}

static const char *reason_code(WhdTlvTraceReason r)
{
    switch (r) {
    case WHDTLV_TRACE_REASON_WINNER:               return "winner";
    case WHDTLV_TRACE_REASON_LOST_SCORE:           return "lost_score";
    case WHDTLV_TRACE_REASON_NO_SCORE:             return "no_score";
    case WHDTLV_TRACE_REASON_REJECTED_EXCLUDE:     return "rejected_exclude";
    case WHDTLV_TRACE_REASON_NOT_LANE_ELIGIBLE:    return "not_lane_eligible";
    case WHDTLV_TRACE_REASON_DUPLICATE_SUPPRESSED: return "dup_suppressed";
    case WHDTLV_TRACE_REASON_SEARCH_GROUP_SKIPPED: return "search_group_skipped";
    default:                                        return "unknown";
    }
}

/*========================================================================*/
/* Explicit token collector                                               */
/*========================================================================*/

/* Scan the variant's stored fields for all values matching bf->tlv_field_id.
 * Appends resolved token strings (semicolon-joined) into tok_buf.
 * Returns the number of explicit values found (0 = no explicit value). */
static int collect_explicit_tokens(const WhdVariantView *v,
                                    const WhdBoundField  *bf,
                                    GlobalCSVManager     *mgr,
                                    const ProfFieldInfo  *pfi,
                                    char *tok_buf, size_t tok_cap)
{
    SBuf     sb;
    int      count = 0;
    uint16_t fvi;

    sb_init(&sb, tok_buf, tok_cap);

    for (fvi = 0; fvi < v->field_count; ++fvi) {
        uint32_t    u32;
        uint16_t    tid;
        const char *tok;
        char        tmp[16];

        if (v->fields[fvi].field_id != bf->tlv_field_id) continue;
        if (v->fields[fvi].length < 4u) continue;

        u32 = read_u32_le_local(v->fields[fvi].value);
        tid = (uint16_t)(u32 & 0xFFFFu);
        tok = resolve_token(mgr, pfi, tid);

        if (count > 0) sb_char(&sb, ';');
        if (tok) {
            sb_str(&sb, tok);
        } else {
            snprintf(tmp, sizeof(tmp), "id:%u", (unsigned)tid);
            sb_str(&sb, tmp);
        }
        ++count;
    }

    return count;
}

/*========================================================================*/
/* CSV header row                                                         */
/*========================================================================*/

static void write_header(FILE *f, const WhdBoundProfile *profile,
                          uint8_t field_count)
{
    uint8_t fi;

    fprintf(f,
        "selected_marker,"
        "selected_rank,"
        "reason_code,"
        "selection_lane,"
        "lane_requirements,"
        "group_id,"
        "group_name,"
        "display_name,"
        "score_total,"
        "reject_field,"
        "lost_to_display_name,"
        "lost_to_score");

    for (fi = 0; fi < field_count; ++fi) {
        const char *fn = profile->fields[fi].field_name;
        fprintf(f, ",%s,%s_effective,%s_effective_source", fn, fn, fn);
    }

    fputc('\n', f);
}

/*========================================================================*/
/* Data row writer                                                        */
/*========================================================================*/

static void write_trace_row(FILE *f,
                             const WhdTlvSelectionTraceRow *row,
                             const WhdVariantArray  *arr,
                             const WhdGroupSet      *gs,
                             const WhdBoundProfile  *profile,
                             const WhdSelectionPlan *plan,
                             const TlvRuntime       *rt,
                             GlobalCSVManager       *mgr,
                             const ProfFieldInfo    *finfo,
                             WhdTlvProfileReportSummary *sum)
{
    const WhdVariantView  *v;
    const WhdVariantGroup *grp;
    const char            *grp_name;
    char                   lane_req_buf[512];
    char                   tok_buf[512];
    char                   eff_buf[256];
    uint8_t                fi;
    int                    is_winner;
    int                    is_loser;

    /* Bounds check */
    if (row->variant_index >= arr->count || row->group_index >= gs->group_count)
        return;

    v   = &arr->items[row->variant_index];
    grp = &gs->groups[row->group_index];

    /* Resolve group name: canonical map first, then base_name fallback */
    grp_name = NULL;
    if (rt->has_group_map && grp->group_id != 0u)
        grp_name = tlv_runtime_group_name(rt, grp->group_id);
    if (!grp_name)
        grp_name = grp->group_name ? grp->group_name : "";

    /* Build lane requirements string */
    lane_req_buf[0] = '\0';
    if (row->lane_index != 0xFFFFFFFFUL) {
        build_lane_req_str(profile, plan, row->lane_index,
                           mgr, finfo, lane_req_buf, sizeof(lane_req_buf));
    }

    is_winner = (row->reason == WHDTLV_TRACE_REASON_WINNER);
    is_loser  = (row->reason == WHDTLV_TRACE_REASON_LOST_SCORE ||
                 row->reason == WHDTLV_TRACE_REASON_NO_SCORE);

    /* ---- Fixed columns ---- */

    /* selected_marker */
    write_str_cell(f, reason_marker(row->reason), 0);

    /* selected_rank */
    fprintf(f, "%d,", is_winner ? 1 : 0);

    /* reason_code */
    write_str_cell(f, reason_code(row->reason), 0);

    /* selection_lane */
    if (row->lane_index == 0xFFFFFFFFUL)
        write_empty_cell(f, 0);
    else
        write_ulong_cell(f, row->lane_index, 0);

    /* lane_requirements */
    write_str_cell(f, lane_req_buf, 0);

    /* group_id */
    fprintf(f, "%u,", (unsigned)grp->group_id);

    /* group_name */
    write_str_cell(f, grp_name, 0);

    /* display_name */
    write_str_cell(f, v->filename ? v->filename : "", 0);

    /* score_total: only meaningful for scored variants */
    if (is_winner || is_loser)
        write_ulong_cell(f, row->score_total, 0);
    else
        write_empty_cell(f, 0);

    /* reject_field */
    if (row->reason == WHDTLV_TRACE_REASON_REJECTED_EXCLUDE &&
        row->reject_field_index < profile->field_count)
        write_str_cell(f, profile->fields[row->reject_field_index].field_name, 0);
    else
        write_empty_cell(f, 0);

    /* lost_to_display_name */
    if (is_loser &&
        row->lost_to_variant_index != 0xFFFFFFFFUL &&
        row->lost_to_variant_index < arr->count) {
        const WhdVariantView *w = &arr->items[row->lost_to_variant_index];
        write_str_cell(f, w->filename ? w->filename : "", 0);
    } else {
        write_empty_cell(f, 0);
    }

    /* lost_to_score */
    if (is_loser && row->lost_to_variant_index != 0xFFFFFFFFUL)
        write_ulong_cell(f, row->lost_to_score, 0);
    else
        write_empty_cell(f, 0);

    /* ---- Per-profile-field effective columns ---- */
    for (fi = 0; fi < profile->field_count; ++fi) {
        const WhdBoundField *bf      = &profile->fields[fi];
        const ProfFieldInfo *pfi     = &finfo[fi];
        int                  is_last = (fi == (uint8_t)(profile->field_count - 1u));
        int                  explicit_count;

        tok_buf[0] = '\0';
        explicit_count = collect_explicit_tokens(v, bf, mgr, pfi,
                                                  tok_buf, sizeof(tok_buf));

        if (explicit_count > 0) {
            /* Explicit value(s) stored in TLV */
            write_str_cell(f, tok_buf, 0);     /* <field>: raw explicit tokens */
            write_str_cell(f, tok_buf, 0);     /* <field>_effective: same      */
            write_str_cell(f, "explicit", is_last);
        } else if (bf->has_default) {
            /* No explicit value, but profile CSV provides a default */
            const char *def_tok = resolve_token(mgr, pfi, bf->default_token_id);
            if (def_tok) {
                strncpy(eff_buf, def_tok, sizeof(eff_buf) - 1);
                eff_buf[sizeof(eff_buf) - 1] = '\0';
            } else {
                snprintf(eff_buf, sizeof(eff_buf), "id:%u",
                         (unsigned)bf->default_token_id);
            }
            write_empty_cell(f, 0);             /* <field>: no explicit          */
            write_str_cell(f, eff_buf, 0);      /* <field>_effective: default tok */
            write_str_cell(f, "default", is_last);
        } else {
            /* No explicit value, no CSV default */
            write_empty_cell(f, 0);             /* <field>                        */
            write_empty_cell(f, 0);             /* <field>_effective              */
            write_str_cell(f, "missing", is_last);
        }
    }

    fputc('\n', f);

    /* Update summary counters */
    if (sum) {
        sum->rows_written++;
        switch (row->reason) {
        case WHDTLV_TRACE_REASON_WINNER:
            sum->winners++;
            break;
        case WHDTLV_TRACE_REASON_LOST_SCORE:
        case WHDTLV_TRACE_REASON_NO_SCORE:
            sum->losers++;
            break;
        case WHDTLV_TRACE_REASON_REJECTED_EXCLUDE:
            sum->rejected++;
            break;
        case WHDTLV_TRACE_REASON_NOT_LANE_ELIGIBLE:
            sum->not_eligible++;
            break;
        case WHDTLV_TRACE_REASON_DUPLICATE_SUPPRESSED:
            sum->dup_suppressed++;
            break;
        default:
            break;
        }
    }
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int whdtlv_report_profile_file(
    const WhdTlvProfileReportOptions *opts,
    WhdTlvProfileReportSummary       *summary)
{
    ProfCtx              ctx;
    WhdBoundProfile      profile;
    WhdSelectionPlan     plan;
    WhdGroupAllowList    allow;
    WhdGroupAllowList   *allow_ptr = NULL;
    WhdTlvSelectionTrace trace;
    WhdSelectResult      result;
    ProfFieldInfo        finfo[PB_MAX_FIELDS];
    FILE                *f = NULL;
    int                  rc;
    int                  has_allow = 0;
    unsigned long        i;

    /* Validate required arguments */
    if (!opts ||
        !opts->tlv_path    || opts->tlv_path[0]    == '\0' ||
        !opts->defs_dir    || opts->defs_dir[0]    == '\0' ||
        !opts->profile_path || opts->profile_path[0] == '\0' ||
        !opts->output_csv_path || opts->output_csv_path[0] == '\0')
        return WHDTLV_PROFILE_REPORT_ERR_BAD_ARG;

    /* Zero-initialize everything so cleanup is always safe */
    memset(&ctx,     0, sizeof(ctx));
    memset(&profile, 0, sizeof(profile));
    memset(&plan,    0, sizeof(plan));
    memset(&allow,   0, sizeof(allow));
    memset(&trace,   0, sizeof(trace));
    memset(&result,  0, sizeof(result));
    memset(finfo,    0, sizeof(finfo));
    if (summary) memset(summary, 0, sizeof(*summary));

    /* ------------------------------------------------------------------
     * 1. Load TLV runtime
     * ------------------------------------------------------------------ */
    tlv_runtime_init(&ctx.rt);
    rc = tlv_runtime_load(&ctx.rt, opts->tlv_path);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr, "whdtlv_report_profile: cannot load TLV '%s' (rc=%d)\n",
                opts->tlv_path, rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 2. Resolve field IDs
     * ------------------------------------------------------------------ */
    ctx.display_fid  = tlv_runtime_field_id(&ctx.rt, "display_name");
    ctx.group_id_fid = ctx.rt.group_id_field_id;

    if (ctx.display_fid == 0u) {
        fprintf(stderr,
                "whdtlv_report_profile: display_name field not found in TLV\n");
        rc = WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 3. Build variant array
     * ------------------------------------------------------------------ */
    rc = tlv_variant_build(&ctx.arr,
                            ctx.rt.reader.buffer + ctx.rt.data_offset,
                            ctx.rt.reader.size   - ctx.rt.data_offset,
                            ctx.display_fid,
                            ctx.group_id_fid);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr,
                "whdtlv_report_profile: variant build failed (rc=%d)\n", rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE;
        goto cleanup;
    }
    ctx.arr_built = 1;

    /* ------------------------------------------------------------------
     * 4. Initialise CSV cache manager
     * ------------------------------------------------------------------ */
    if (!csv_cache_manager_init(&ctx.mgr, NULL, opts->defs_dir)) {
        rc = WHDTLV_PROFILE_REPORT_ERR_OOM;
        goto cleanup;
    }
    ctx.mgr_init = 1;

    /* ------------------------------------------------------------------
     * 5. Build group set
     * ------------------------------------------------------------------ */
    rc = tlv_group_build(&ctx.gs, &ctx.arr, (ctx.group_id_fid != 0u) ? 1 : 0);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr,
                "whdtlv_report_profile: group build failed (rc=%d)\n", rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE;
        goto cleanup;
    }
    ctx.gs_built = 1;

    /* ------------------------------------------------------------------
     * 6. Load and bind the profile
     * ------------------------------------------------------------------ */
    rc = whd_profile_load(opts->profile_path, &ctx.rt, opts->defs_dir,
                           &profile);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr,
                "whdtlv_report_profile: profile load failed '%s' (rc=%d)\n",
                opts->profile_path, rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_PROFILE;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 7. Build selection plan (generates per-lane requirement sets)
     * ------------------------------------------------------------------ */
    rc = whd_build_selection_plan(&profile, &plan);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr,
                "whdtlv_report_profile: selection plan failed (rc=%d)\n", rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_PROFILE;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 8. Build search allow list (optional)
     * ------------------------------------------------------------------ */
    if (opts->search_pattern && opts->search_pattern[0] != '\0') {
        WhdSearchRequest req;
        req.pattern = opts->search_pattern;
        req.flags   = WHD_SEARCHF_ENABLED
                    | WHD_SEARCHF_CASE_INSENSITIVE
                    | WHD_SEARCHF_GROUP_NAME
                    | WHD_SEARCHF_DISPLAY_NAME;
        rc = whd_search_build_group_allow_list(&ctx.rt, &ctx.gs, &req, &allow);
        if (rc != WHD_FILTER_OK) {
            rc = WHDTLV_PROFILE_REPORT_ERR_OOM;
            goto cleanup;
        }
        allow_ptr = &allow;
        has_allow = 1;
    }

    /* ------------------------------------------------------------------
     * 9. Run traced selector
     * ------------------------------------------------------------------ */
    whdtlv_trace_init(&trace);

    rc = tlv_select_run_traced(&result, &ctx.gs, &ctx.arr, &profile,
                                allow_ptr, &trace);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr,
                "whdtlv_report_profile: selection run failed (rc=%d)\n", rc);
        rc = WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 10. Pre-load CSVs for profile fields
     * ------------------------------------------------------------------ */
    build_field_info(finfo, profile.field_count, &profile, &ctx.rt, &ctx.mgr);

    /* ------------------------------------------------------------------
     * 11. Open output CSV
     * ------------------------------------------------------------------ */
    f = fopen(opts->output_csv_path, "w");
    if (!f) {
        fprintf(stderr,
                "whdtlv_report_profile: cannot open output '%s'\n",
                opts->output_csv_path);
        rc = WHDTLV_PROFILE_REPORT_ERR_CSV_OPEN;
        goto cleanup;
    }

    /* ------------------------------------------------------------------
     * 12. Write header row
     * ------------------------------------------------------------------ */
    write_header(f, &profile, profile.field_count);

    /* ------------------------------------------------------------------
     * 13. Walk trace rows and emit CSV
     * ------------------------------------------------------------------ */
    for (i = 0; i < trace.count; ++i) {
        write_trace_row(f, &trace.rows[i],
                        &ctx.arr, &ctx.gs, &profile, &plan,
                        &ctx.rt, &ctx.mgr, finfo, summary);
    }

    /* Fill in group/variant totals */
    if (summary) {
        summary->groups_total   = ctx.gs.group_count;
        summary->variants_total = ctx.arr.count;
    }

    rc = WHDTLV_PROFILE_REPORT_OK;

cleanup:
    if (f)           fclose(f);
    whdtlv_trace_free(&trace);
    tlv_select_free(&result);
    if (has_allow)   whd_group_allow_list_free(&allow);
    if (ctx.gs_built) tlv_group_free(&ctx.gs);
    if (ctx.arr_built) tlv_variant_free(&ctx.arr);
    if (ctx.mgr_init) csv_cache_manager_cleanup(&ctx.mgr);
    tlv_runtime_free(&ctx.rt);
    return rc;
}

/* End of Text */
