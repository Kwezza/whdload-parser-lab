/* tools_src/filter_harness/main.c - CLI wrapper for the filter facade
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Host-only fixture discovery tool.  Runs whdtlv_filter_to_list() and writes:
 *   - one selected filename per line to <out_file>
 *   - key counters in key=value format to <summary_file>
 *
 * Usage:
 *   filter_harness <tlv> <defs> <profile> <search> <out_file> <summary_file>
 *
 *   profile  : path to .profile, or "" for built-in defaults
 *   search   : search pattern, or "" for no search
 *
 * Exit codes:
 *   0   Success (may be empty result set)
 *   1   Wrong argument count
 *   2   Filter pipeline error (negative rc from whdtlv_filter_to_list)
 *   3   Could not open output file
 *   4   Could not open summary file
 */

#include "whdtlv/whdtlv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *nonempty(const char *s)
{
    return (s && s[0] != '\0') ? s : NULL;
}

int main(int argc, char *argv[])
{
    const char             *tlv_path;
    const char             *defs_dir;
    const char             *profile_path;
    const char             *search_pattern;
    const char             *out_file;
    const char             *summary_file;
    WhdTlvFilterOptions     opts;
    WhdTlvFilterSummary     summary;
    WhdTlvStringList        results;
    FILE                   *fp;
    unsigned int            i;
    int                     rc;

    if (argc != 7) {
        fprintf(stderr,
            "Usage: %s <tlv> <defs> <profile> <search> <out_file> <summary_file>\n"
            "  profile : path to .profile, or \"\" for built-in defaults\n"
            "  search  : search pattern, or \"\" for no search\n",
            argv[0]);
        return 1;
    }

    tlv_path       = argv[1];
    defs_dir       = argv[2];
    profile_path   = nonempty(argv[3]);
    search_pattern = nonempty(argv[4]);
    out_file       = argv[5];
    summary_file   = argv[6];

    whdtlv_filter_options_defaults(&opts);
    memset(&summary, 0, sizeof(summary));
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        tlv_path, defs_dir, profile_path,
        search_pattern, &opts, &results, &summary);

    if (rc != WHDTLV_OK) {
        fprintf(stderr, "filter_harness: pipeline error rc=%d\n", rc);
        return 2;
    }

    /* Write selected filenames */
    fp = fopen(out_file, "w");
    if (!fp) {
        fprintf(stderr, "filter_harness: cannot open output file '%s'\n", out_file);
        whdtlv_string_list_free(&results);
        return 3;
    }
    for (i = 0u; i < results.count; ++i) {
        if (results.items[i]) {
            fprintf(fp, "%s\n", results.items[i]);
        }
    }
    fclose(fp);
    whdtlv_string_list_free(&results);

    /* Write summary */
    fp = fopen(summary_file, "w");
    if (!fp) {
        fprintf(stderr, "filter_harness: cannot open summary file '%s'\n", summary_file);
        return 4;
    }
    fprintf(fp, "profile=%s\n",          profile_path   ? profile_path   : "");
    fprintf(fp, "search=%s\n",           search_pattern ? search_pattern : "");
    fprintf(fp, "tlv=%s\n",              tlv_path);
    fprintf(fp, "matched_groups=%u\n",   summary.matched_groups);
    fprintf(fp, "selected_variants=%u\n",summary.selected_variants);
    fprintf(fp, "selected_groups=%u\n",  summary.selected_groups);
    fprintf(fp, "selection_lanes=%u\n",  summary.selection_lanes);
    fprintf(fp, "variants_total=%u\n",   summary.variants_total);
    fprintf(fp, "groups_total=%u\n",     summary.groups_total);
    fclose(fp);

    printf("filter_harness: rc=0  matched=%u selected=%u lanes=%u -> %s\n",
        summary.matched_groups, summary.selected_variants,
        summary.selection_lanes, out_file);

    return 0;
}
