/* tools/tlv_demo/tlv_demo.c - Minimal facade demonstration program
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Demonstrates the public whdtlv facade.  Includes only the public
 * integration header; no internal pipeline headers are used here.
 *
 * Usage:
 *   tlv_demo <dat_path> <defs_dir> <pack_types_path> <output_tlv_path>
 *
 * Exit codes:
 *   0  Success
 *   1  Wrong number of arguments
 *   2  whdtlv_build_from_dat returned an error
 */

#include <whdtlv/whdtlv.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    WhdTlvBuildOptions opts;
    WhdTlvBuildSummary summary;
    int                rc;

    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <dat_path> <defs_dir> <pack_types_path> <output_tlv_path>\n",
                argv[0]);
        return 1;
    }

    whdtlv_build_options_defaults(&opts);

    rc = whdtlv_build_from_dat(
        argv[1],   /* dat_path        */
        argv[2],   /* defs_dir        */
        argv[3],   /* pack_types_path */
        argv[4],   /* output_tlv_path */
        0u,        /* pack_type_id    — 0 = default */
        &opts,
        &summary
    );

    if (rc != WHDTLV_OK) {
        fprintf(stderr, "whdtlv_build_from_dat failed (error %d)\n", rc);
        return 2;
    }

    printf("records_written : %u\n", summary.records_written);
    printf("records_skipped : %u\n", summary.records_skipped);
    printf("groups_assigned : %u\n", summary.groups_assigned);

    return 0;
}
