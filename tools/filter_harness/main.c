/* tools/filter_harness/main.c - Command-line test harness for TLV filtering
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * This is a test wrapper only.  All reusable filtering logic lives in
 * src_raw/filtering/.  The harness owns argument parsing, console output,
 * and result-file coordination.  It must not contain logic that belongs
 * in the reusable subsystem.
 *
 * Stage E deliverable: --profile + --dump-profile bind and display profile fields.
 */

#include <filtering/tlv_filter.h>
#include <filtering/tlv_runtime.h>
#include <filtering/tlv_crc_validate.h>
#include <filtering/profile_binder.h>
#include <filtering/tlv_variant.h>
#include <filtering/tlv_group.h>
#include <filtering/tlv_select.h>
#include <filtering/tlv_results.h>
#include <filtering/whd_search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Usage                                                                  */

static void print_usage(const char *prog)
{
    printf("filter_harness - TLV variant selection harness\n\n");
    printf("Usage:\n");
    printf("  %s --tlv <file> --profile <file> --out <file> [options]\n", prog);
    printf("  %s --help\n\n", prog);
    printf("Options:\n");
    printf("  --tlv <file>        TLV file to filter\n");
    printf("  --profile <file>    Profile file (.profile)\n");
    printf("  --defs <dir>        CSV definitions directory"
           " (default: assets_raw/defs)\n");
    printf("  --packtypes <file>  pack_types.ini path\n");
    printf("  --out <file>        Output filename list\n");
    printf("  --strict-crc        Abort on any CSV CRC mismatch (default)\n");
    printf("  --warn-crc          Warn on CRC mismatch but continue\n");
    printf("  --dump-header       Print TLV header info\n");
    printf("  --dump-fields       Print TLV field map\n");
    printf("  --dump-crcs         Print TLV CSV fingerprints\n");
    printf("  --dump-profile      Print bound profile\n");  printf("  --search <pattern>  Pre-filter groups by name (supports * and ?)\n");    printf("  --dump-groups       Print variant groups\n");
    printf("  --dump-rejected     Print rejected variants\n");
    printf("  --group <name>      Score and display single named group\n");
    printf("  --limit <n>         Limit output to first n entries\n");
}

/*------------------------------------------------------------------------*/
/* Parsed argument state                                                  */

typedef struct HarnessArgs {
    const char   *tlv_path;
    const char   *profile_path;
    const char   *defs_dir;
    const char   *pack_types_path;
    const char   *output_path;
    const char   *group_name;
    const char   *search_pattern; /* NULL = no search pre-filter */
    unsigned int  flags;
    unsigned long limit;
    int           dump_header;
    int           dump_fields;
    int           dump_crcs;
    int           dump_profile;
    int           dump_groups;
    int           dump_rejected;
} HarnessArgs;

static void args_init(HarnessArgs *a)
{
    a->tlv_path        = NULL;
    a->profile_path    = NULL;
    a->defs_dir        = "assets_raw/defs";
    a->pack_types_path = "assets_raw/prefs/pack_types.ini";
    a->output_path     = NULL;
    a->group_name      = NULL;
    a->search_pattern  = NULL;
    a->flags           = WHD_FILTER_CRC_STRICT;
    a->limit           = 0;
    a->dump_header     = 0;
    a->dump_fields     = 0;
    a->dump_crcs       = 0;
    a->dump_profile    = 0;
    a->dump_groups     = 0;
    a->dump_rejected   = 0;
}

/*------------------------------------------------------------------------*/
/* Argument parsing                                                       */
/*
 * Returns  0 on success.
 * Returns -1 if --help was requested (caller prints usage and exits 0).
 * Returns  1 on unrecognised or malformed argument.
 */

static int parse_args(int argc, char **argv, HarnessArgs *a)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return -1;
        } else if (strcmp(argv[i], "--tlv") == 0 && i + 1 < argc) {
            a->tlv_path = argv[++i];
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            a->profile_path = argv[++i];
        } else if (strcmp(argv[i], "--defs") == 0 && i + 1 < argc) {
            a->defs_dir = argv[++i];
        } else if (strcmp(argv[i], "--packtypes") == 0 && i + 1 < argc) {
            a->pack_types_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            a->output_path = argv[++i];
        } else if (strcmp(argv[i], "--group") == 0 && i + 1 < argc) {
            a->group_name = argv[++i];
        } else if (strcmp(argv[i], "--search") == 0 && i + 1 < argc) {
            a->search_pattern = argv[++i];
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            a->limit = (unsigned long)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--strict-crc") == 0) {
            a->flags = (a->flags & ~WHD_FILTER_CRC_WARNONLY) | WHD_FILTER_CRC_STRICT;
        } else if (strcmp(argv[i], "--warn-crc") == 0) {
            a->flags = (a->flags & ~WHD_FILTER_CRC_STRICT) | WHD_FILTER_CRC_WARNONLY;
        } else if (strcmp(argv[i], "--dump-header") == 0) {
            a->dump_header = 1;
        } else if (strcmp(argv[i], "--dump-fields") == 0) {
            a->dump_fields = 1;
        } else if (strcmp(argv[i], "--dump-crcs") == 0) {
            a->dump_crcs = 1;
        } else if (strcmp(argv[i], "--dump-profile") == 0) {
            a->dump_profile = 1;
        } else if (strcmp(argv[i], "--dump-groups") == 0) {
            a->dump_groups = 1;
        } else if (strcmp(argv[i], "--dump-rejected") == 0) {
            a->dump_rejected = 1;
        } else {
            fprintf(stderr, "ERROR: unknown argument: %s\n", argv[i]);
            return 1;
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* CRC validation helper                                                  */
/*
 * Runs CSV CRC validation and prints a one-line summary.
 * Returns 0 on success (all CRCs matched or warn-only mode).
 * Returns 1 if strict mode aborted due to a mismatch/missing file.
 */
static int run_crc_validation(const TlvRuntime *rt,
                              const char       *defs_dir,
                              unsigned int      flags)
{
    WhdCrcValidateResult crc_result;
    int                  rc;

    rc = tlv_crc_validate(rt, defs_dir, flags, &crc_result);

    if (crc_result.no_crc_block) {
        printf("CSV CRC: WARNING: TLV has no CRC fingerprint block\n");
        return 0;
    }

    if (rc != WHD_FILTER_OK) {
        /* Error message already printed by tlv_crc_validate */
        printf("CSV CRC: FAILED\n");
        return 1;
    }

    if (crc_result.missing_count > 0 || crc_result.mismatch_count > 0 ||
        crc_result.unreadable_count > 0) {
        printf("CSV CRC: WARNINGS  ok=%lu  missing=%lu  unreadable=%lu  mismatch=%lu\n",
               crc_result.ok_count,
               crc_result.missing_count,
               crc_result.unreadable_count,
               crc_result.mismatch_count);
    } else {
        printf("CSV CRC: OK  (%lu files checked)\n", crc_result.ok_count);
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Dump helpers                                                           */

static void dump_header(const TlvRuntime *rt)
{
    printf("--- TLV Header ---\n");
    printf("  Format version : %u\n",  (unsigned)rt->header.version);
    printf("  Field map      : %s\n",  rt->has_field_map ? "yes" : "no");
    printf("  CRC block      : %s\n",  rt->has_crc_map   ? "yes" : "no");
    if (rt->has_group_map) {
        printf("  Group map      : yes (%lu entries)\n",
               rt->group_map.count);
    } else {
        printf("  Group map      : no\n");
    }
    if (rt->group_id_field_id != 0u) {
        printf("  Grouping mode  : group_id field (0x%02X)\n",
               (unsigned)rt->group_id_field_id);
    } else {
        printf("  Grouping mode  : display_name heuristic (no group_id in field map)\n");
    }
    printf("  Data offset    : %lu bytes\n", rt->data_offset);
    printf("  File size      : %lu bytes\n", rt->reader.size);
}

static void dump_fields(const TlvRuntime *rt)
{
    uint8_t i;
    printf("--- Field Map (%u fields) ---\n", (unsigned)rt->field_map.count);
    for (i = 0; i < rt->field_map.count; i++) {
        printf("  id=0x%02X  name=%s\n",
               (unsigned)rt->field_map.entries[i].id,
               rt->field_map.entries[i].name);
    }
}

static void dump_crcs(const TlvRuntime *rt)
{
    unsigned long i;
    if (!rt->has_crc_map) {
        printf("--- CRC Block: not present in TLV ---\n");
        return;
    }
    printf("--- CSV CRC Fingerprints (%lu entries) ---\n", rt->crc_map.count);
    for (i = 0; i < rt->crc_map.count; i++) {
        printf("  %s  crc=%08lX\n",
               rt->crc_map.entries[i].csv_name,
               (unsigned long)rt->crc_map.entries[i].crc32);
    }
}

static int run_profile_bind(const TlvRuntime *rt,
                            const char       *profile_path,
                            const char       *defs_dir,
                            WhdBoundProfile  *out)
{
    int rc = whd_profile_load(profile_path, rt, defs_dir, out);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr, "ERROR: %s: %s\n",
                whd_filter_error_string(rc), profile_path);
        return 1;
    }
    if (out->had_warnings) {
        fprintf(stderr, "WARNING: profile had unknown fields or tokens\n");
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Stage F+G: dump variant groups                                         */

static void dump_groups(const TlvRuntime     *rt,
                        const WhdGroupSet    *gs,
                        const WhdVariantArray *arr,
                        unsigned long         limit)
{
    unsigned long g;
    unsigned long max_groups;
    const char   *mode_str;

    mode_str = gs->used_group_id_field
        ? "group_id field"
        : "display_name heuristic";

    printf("--- Variant Groups: %lu groups, %lu variants  [%s] ---\n",
           gs->group_count, gs->total_variants, mode_str);
    if (gs->fallback_count > 0u) {
        printf("--- Fallback group derivations: %lu ---\n", gs->fallback_count);
    }

    max_groups = gs->group_count;
    if (limit > 0u && limit < max_groups) {
        max_groups = limit;
    }

    for (g = 0u; g < max_groups; g++) {
        const WhdVariantGroup *grp = &gs->groups[g];
        unsigned long          vi;
        const char            *display_name;

        /* Use the canonical group map name when available; otherwise fall
         * back to the heuristic base_name stored in the group descriptor. */
        if (gs->used_group_id_field && rt && rt->has_group_map) {
            display_name = tlv_runtime_group_name(rt, grp->group_id);
            if (!display_name) {
                display_name = grp->group_name; /* base_name fallback */
            }
        } else {
            display_name = grp->group_name;
        }
        if (!display_name) {
            display_name = "<unnamed>";
        }

        if (gs->used_group_id_field) {
            /* Show 4-digit zero-padded group_id in brackets. */
            printf("  [%04lu] %s  (%lu variant%s)\n",
                   (unsigned long)grp->group_id,
                   display_name,
                   grp->variant_count,
                   grp->variant_count == 1u ? "" : "s");
        } else {
            printf("  [%lu] %s  (%lu variant%s)\n",
                   g + 1u,
                   display_name,
                   grp->variant_count,
                   grp->variant_count == 1u ? "" : "s");
        }

        for (vi = 0u; vi < grp->variant_count; vi++) {
            unsigned long idx = gs->sorted_indices[grp->first_variant + vi];
            const char   *fn  = arr->items[idx].filename;
            printf("      %s\n", fn ? fn : "(null)");
        }
    }

    if (limit > 0u && limit < gs->group_count) {
        printf("  ... (showing %lu of %lu groups)\n",
               limit, gs->group_count);
    }
}

/*------------------------------------------------------------------------*/
/* Stage H: scored group display and rejected-group dump                  */

static void dump_scored_group(const WhdGroupSet     *gs,
                               const WhdVariantArray *arr,
                               const WhdBoundProfile *profile,
                               const WhdSelectResult *sel,
                               const char            *group_name)
{
    unsigned long g;

    for (g = 0u; g < gs->group_count; g++) {
        const WhdVariantGroup *grp = &gs->groups[g];
        unsigned long          vi;

        if (!grp->group_name || strcmp(grp->group_name, group_name) != 0) {
            continue;
        }

        printf("--- Group: %s  (%lu variant%s) ---\n",
               group_name, grp->variant_count,
               grp->variant_count == 1u ? "" : "s");

        for (vi = 0u; vi < grp->variant_count; vi++) {
            unsigned long         arr_idx = gs->sorted_indices[grp->first_variant + vi];
            const WhdVariantView *v       = &arr->items[arr_idx];
            WhdVariantScore       vs;
            int                   is_selected;

            tlv_select_score_variant(&vs, v, profile);
            is_selected = (sel->entries[g].variant_index == arr_idx);

            if (vs.rejected) {
                printf("  [rejected] %s\n",
                       v->filename ? v->filename : "(null)");
            } else if (is_selected) {
                printf("  [selected] %-40s  score=%lu\n",
                       v->filename ? v->filename : "(null)", vs.score);
            } else {
                printf("             %-40s  score=%lu\n",
                       v->filename ? v->filename : "(null)", vs.score);
            }
        }
        return;
    }
    printf("Group '%s' not found\n", group_name);
}

static void dump_rejected_groups(const WhdGroupSet     *gs,
                                  const WhdSelectResult *sel,
                                  unsigned long          limit)
{
    unsigned long g;
    unsigned long shown;

    printf("--- Rejected Groups ---\n");
    shown = 0u;

    for (g = 0u; g < gs->group_count; g++) {
        const WhdSelectEntry *entry = &sel->entries[g];
        if (!entry->all_rejected) {
            continue;
        }
        if (limit > 0u && shown >= limit) {
            break;
        }
        printf("  %s\n",
               gs->groups[g].group_name ? gs->groups[g].group_name : "(null)");
        shown++;
    }

    if (shown == 0u) {
        printf("  (none)\n");
    } else if (limit > 0u && shown >= limit) {
        printf("  ... (limit reached)\n");
    }
}

int main(int argc, char **argv)
{
    HarnessArgs      args;
    WhdFilterRequest request;
    WhdFilterResult  result;
    TlvRuntime       rt;
    int              rc;
    int              any_dump;
    WhdBoundProfile  profile;

    args_init(&args);
    rc = parse_args(argc, argv, &args);

    if (rc < 0 || argc <= 1) {
        print_usage(argv[0]);
        return 0;
    }
    if (rc > 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (!args.tlv_path) {
        fprintf(stderr, "ERROR: --tlv is required\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ------------------------------------------------------------------ */
    /* Load and validate the TLV (Stages B + C)                           */

    tlv_runtime_init(&rt);
    rc = tlv_runtime_load(&rt, args.tlv_path);
    if (rc != WHD_FILTER_OK) {
        fprintf(stderr, "ERROR: %s: %s\n",
                whd_filter_error_string(rc), args.tlv_path);
        return 1;
    }

    any_dump = args.dump_header || args.dump_fields || args.dump_crcs ||
               args.dump_profile || args.dump_groups || args.dump_rejected ||
               (args.group_name != NULL);

    if (args.dump_header) { dump_header(&rt); }
    if (args.dump_fields) { dump_fields(&rt); }
    if (args.dump_crcs)   { dump_crcs(&rt);   }

    /* ------------------------------------------------------------------ */
    /* Stage D: CRC validation                                            */

    if (args.defs_dir) {
        int crc_rc = run_crc_validation(&rt, args.defs_dir, args.flags);
        if (crc_rc != 0) {
            tlv_runtime_free(&rt);
            return 1;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Stage E: Profile load and bind                                     */

    memset(&profile, 0, sizeof(profile));
    if (args.profile_path) {
        int prc = run_profile_bind(&rt, args.profile_path,
                                   args.defs_dir, &profile);
        if (prc != 0) {
            tlv_runtime_free(&rt);
            return 1;
        }
        if (args.dump_profile) {
            whd_profile_dump(&profile);
        }
    } else if (args.dump_profile) {
        printf("Profile: none loaded (use --profile <file>)\n");
    }

    /* ------------------------------------------------------------------ */
    /* Stage F+G: Variant scan and grouping                               */

    if (args.dump_groups) {
        WhdVariantArray arr;
        WhdGroupSet     gs;
        uint8_t         display_fid;
        int             vrc;

        display_fid = tlv_runtime_field_id(&rt, "display_name");
        if (display_fid == 0) {
            fprintf(stderr, "ERROR: TLV field map has no 'display_name' field\n");
            tlv_runtime_free(&rt);
            return 1;
        }

        vrc = tlv_variant_build(&arr,
                                rt.reader.buffer + rt.data_offset,
                                rt.reader.size   - rt.data_offset,
                                display_fid,
                                rt.group_id_field_id);
        if (vrc != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: tlv_variant_build: %s\n",
                    whd_filter_error_string(vrc));
            tlv_runtime_free(&rt);
            return 1;
        }

        vrc = tlv_group_build(&gs, &arr, (rt.group_id_field_id != 0u));
        if (vrc != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: tlv_group_build: %s\n",
                    whd_filter_error_string(vrc));
            tlv_variant_free(&arr);
            tlv_runtime_free(&rt);
            return 1;
        }

        dump_groups(&rt, &gs, &arr, args.limit);

        tlv_group_free(&gs);
        tlv_variant_free(&arr);
    }

    /* ------------------------------------------------------------------ */
    /* Stage H: Score a named group or dump rejected variants             */

    if (args.profile_path && (args.group_name || args.dump_rejected)) {
        WhdVariantArray   arr2;
        WhdGroupSet       gs2;
        WhdSelectResult   sel2;
        WhdGroupAllowList h_allow;
        uint8_t           disp_fid;
        int               src;
        int               h_has_search;

        h_has_search = 0;
        memset(&h_allow, 0, sizeof(h_allow));

        disp_fid = tlv_runtime_field_id(&rt, "display_name");
        if (disp_fid == 0) {
            fprintf(stderr, "ERROR: TLV has no 'display_name' field\n");
            tlv_runtime_free(&rt);
            return 1;
        }

        src = tlv_variant_build(&arr2,
                                 rt.reader.buffer + rt.data_offset,
                                 rt.reader.size   - rt.data_offset,
                                 disp_fid,
                                 rt.group_id_field_id);
        if (src != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: tlv_variant_build: %s\n",
                    whd_filter_error_string(src));
            tlv_runtime_free(&rt);
            return 1;
        }

        src = tlv_group_build(&gs2, &arr2, (rt.group_id_field_id != 0u));
        if (src != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: tlv_group_build: %s\n",
                    whd_filter_error_string(src));
            tlv_variant_free(&arr2);
            tlv_runtime_free(&rt);
            return 1;
        }

        /* Search pre-filter (Stage 4 wire-up) */
        if (args.search_pattern && args.search_pattern[0] != '\0') {
            WhdSearchRequest sreq;
            sreq.pattern = args.search_pattern;
            sreq.flags   = WHD_SEARCHF_ENABLED | WHD_SEARCHF_CASE_INSENSITIVE;
            src = whd_search_build_group_allow_list(&rt, &gs2, &sreq, &h_allow);
            if (src != WHD_FILTER_OK) {
                fprintf(stderr, "ERROR: search pre-filter: %s\n",
                        whd_filter_error_string(src));
                tlv_group_free(&gs2);
                tlv_variant_free(&arr2);
                tlv_runtime_free(&rt);
                return 1;
            }
            h_has_search = 1;
            printf("Search         : %s\n", args.search_pattern);
            printf("Matched groups : %lu\n", h_allow.matched_count);
        }

        src = tlv_select_run(&sel2, &gs2, &arr2, &profile,
                              h_has_search ? &h_allow : NULL);
        if (src != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: tlv_select_run: %s\n",
                    whd_filter_error_string(src));
            if (h_has_search) { whd_group_allow_list_free(&h_allow); }
            tlv_group_free(&gs2);
            tlv_variant_free(&arr2);
            tlv_runtime_free(&rt);
            return 1;
        }

        if (args.group_name) {
            dump_scored_group(&gs2, &arr2, &profile, &sel2, args.group_name);
        }
        if (args.dump_rejected) {
            dump_rejected_groups(&gs2, &sel2, args.limit);
        }

        tlv_select_free(&sel2);
        if (h_has_search) { whd_group_allow_list_free(&h_allow); }
        tlv_group_free(&gs2);
        tlv_variant_free(&arr2);
    }

    /* ------------------------------------------------------------------ */
    /* Stage I: Full pipeline — write output file                         */

    if (!any_dump && args.output_path) {
        request.tlv_path        = args.tlv_path;
        request.profile_path    = args.profile_path;
        request.defs_dir        = args.defs_dir;
        request.pack_types_path = args.pack_types_path;
        request.output_path     = args.output_path;
        request.search_pattern  = args.search_pattern;
        request.flags           = args.flags;

        rc = whd_filter_run(&request, &result);
        if (rc != WHD_FILTER_OK) {
            fprintf(stderr, "ERROR: %s\n", whd_filter_error_string(rc));
            tlv_runtime_free(&rt);
            return 1;
        }
        if (args.search_pattern && args.search_pattern[0] != '\0') {
            printf("Search         : %s\n", args.search_pattern);
            printf("Matched groups : %lu\n", result.search_matched_groups);
        }
        tlv_results_print_summary(args.tlv_path, args.profile_path,
                                   args.output_path, &result);
    }

    tlv_runtime_free(&rt);
    return 0;
}

/* End of Text */
