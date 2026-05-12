/* whdtlv_integration.h - Public facade for the WHDLoad DAT-to-TLV pipeline
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Single-header public API for embedding the dat-to-TLV pipeline in a host
 * process (e.g. WHDFetch).  Normal callers include only this header and
 * need nothing else from the pipeline layer.
 *
 * C89-compatible types only.  No internal struct types are exposed here.
 */

#ifndef WHDTLV_H
#define WHDTLV_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Return codes */

#define WHDTLV_OK               0
#define WHDTLV_ERR_INVALID_ARG  1
#define WHDTLV_ERR_IO           2
#define WHDTLV_ERR_PARSE        3
#define WHDTLV_ERR_ALLOC        4

/*------------------------------------------------------------------------*/
/* Options struct — zero-initialise to get defaults */

typedef struct WhdTlvBuildOptions {
    int enable_logging;   /* 0 = off, 1 = on */
    int enable_profile;   /* 0 = off, 1 = on (PROFILE build only) */
    int reserved[6];      /* zero-init; reserved for future use */
} WhdTlvBuildOptions;

/*------------------------------------------------------------------------*/
/* Summary struct — filled in by whdtlv_build_from_dat on success */

typedef struct WhdTlvBuildSummary {
    unsigned int records_written;
    unsigned int records_skipped;
    unsigned int groups_assigned;
    unsigned int reserved[5];
} WhdTlvBuildSummary;

/*------------------------------------------------------------------------*/
/* Public API */

/**
 * Populate opts with safe defaults.
 * Call this before whdtlv_build_from_dat to ensure forward compatibility.
 */
void whdtlv_build_options_defaults(WhdTlvBuildOptions *opts);

/**
 * Build a TLV file from a single Logiqx-style WHDLoad DAT file.
 *
 * dat_path        - path to the .dat XML file
 * defs_dir        - directory containing field CSV files (assets_raw/defs/)
 * pack_types_path - path to pack_types.ini
 * output_tlv_path - destination .tlv file (created or overwritten)
 * pack_type_id    - pack type index passed to the session batch processor
 *                   (0 = use first/default pack type)
 * options         - caller-provided options; pass NULL for defaults
 * summary         - output summary; pass NULL to ignore
 *
 * Returns WHDTLV_OK on success, or a WHDTLV_ERR_* code on failure.
 */
int whdtlv_build_from_dat(
    const char                *dat_path,
    const char                *defs_dir,
    const char                *pack_types_path,
    const char                *output_tlv_path,
    unsigned int               pack_type_id,
    const WhdTlvBuildOptions  *options,
    WhdTlvBuildSummary        *summary
);

#ifdef __cplusplus
}
#endif

#endif /* WHDTLV_H */
