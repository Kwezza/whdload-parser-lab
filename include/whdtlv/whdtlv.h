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
/* Filter failure codes — returned by whdtlv_filter_to_list() and        */
/* whdtlv_filter_to_file() on failure (all negative)                     */
/*                                                                       */
/* Callers may distinguish success/failure by sign:                      */
/*   rc == WHDTLV_OK (0)  — success (result list may be empty)           */
/*   rc  < 0              — pipeline failure; do not free results        */

#define WHDTLV_FILTER_ERR_INVALID_ARG  (-1)  /* NULL required argument        */
#define WHDTLV_FILTER_ERR_IO           (-2)  /* TLV open or output write fail */
#define WHDTLV_FILTER_ERR_PARSE        (-3)  /* TLV header / version invalid  */
#define WHDTLV_FILTER_ERR_EMPTY        (-4)  /* TLV has no variants or groups */
#define WHDTLV_FILTER_ERR_CRC          (-5)  /* CSV fingerprint mismatch      */
#define WHDTLV_FILTER_ERR_PROFILE      (-6)  /* profile load or bind failed   */
#define WHDTLV_FILTER_ERR_ALLOC        (-7)  /* out of memory                 */

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
/* In-memory filename list returned by the filtering facade              */

typedef struct WhdTlvStringList {
    unsigned int  count;
    char        **items;
    void         *reserved; /* internal; caller must not inspect or free */
} WhdTlvStringList;

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

/*========================================================================*/
/* Filtering facade                                                       */
/*========================================================================*/

/*------------------------------------------------------------------------*/
/* Filter options — zero-initialise then call whdtlv_filter_options_defaults() */

typedef struct WhdTlvFilterOptions {
    int enable_logging; /* 0 = off, 1 = on (v1: accepted but no effect) */
    int strict_crc;     /* 1 = abort on CSV fingerprint mismatch (default) */
    int reserved[6];    /* must be zero-initialised */
} WhdTlvFilterOptions;

/*------------------------------------------------------------------------*/
/* Filter summary — filled in by whdtlv_filter_to_list() on success     */

typedef struct WhdTlvFilterSummary {
    unsigned int variants_total;
    unsigned int groups_total;
    unsigned int matched_groups;     /* groups matched by search_pattern;
                                       == groups_total if no search was applied;
                                       == 0 if search was applied and matched nothing */

    unsigned int selected_variants;  /* sum of per-group lane winners */
    unsigned int selected_groups;    /* groups with at least one lane satisfied */
    unsigned int rejected_variants;
    unsigned int rejected_groups;
    unsigned int selection_lanes;    /* number of selection lanes used (>=1) */
    unsigned int crc_files_checked;  /* total CSV files examined during CRC validation */
    unsigned int crc_mismatches;
    unsigned int reserved[6];
} WhdTlvFilterSummary;

/*------------------------------------------------------------------------*/
/* Filtering API                                                          */

/**
 * Populate opts with safe defaults:
 *   enable_logging = 0
 *   strict_crc     = 1
 *   reserved[]     = 0
 */
void whdtlv_filter_options_defaults(WhdTlvFilterOptions *opts);

/**
 * Run the TLV filtering pipeline and return selected archive filenames
 * as an in-memory list.
 *
 * tlv_path       - path to a TLV file produced by whdtlv_build_from_dat()
 * defs_dir       - directory containing the CSV definition files
 * profile_path   - path to a .profile file; NULL uses built-in defaults
 * search_pattern - optional group-level pre-filter; NULL or "" = no filter
 *                  plain text: case-insensitive substring match
 *                  * and ? wildcards are supported
 * options        - caller options; pass NULL for defaults
 * results        - output list; caller must free with whdtlv_string_list_free()
 * summary        - output summary; pass NULL to ignore
 *
 * Returns WHDTLV_OK on success (including empty result set).
 * Returns a negative WHDTLV_FILTER_ERR_* code on failure; results is left
 * in a safe empty state.  It is always safe to call whdtlv_string_list_free()
 * after any return from this function, including on failure.
 */
int whdtlv_filter_to_list(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvStringList           *results,
    WhdTlvFilterSummary        *summary
);

/**
 * Convenience wrapper: run the filter pipeline and write one selected
 * filename per line to output_list_path.
 *
 * Calls whdtlv_filter_to_list() internally and writes then frees the list.
 *
 * Returns WHDTLV_OK on success, WHDTLV_FILTER_ERR_IO if the output file
 * cannot be opened, or a negative WHDTLV_FILTER_ERR_* code on pipeline failure.
 */
int whdtlv_filter_to_file(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *output_list_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvFilterSummary        *summary
);

/**
 * Free all memory owned by a WhdTlvStringList returned by whdtlv_filter_to_list().
 *
 * Safe to call on an empty or zeroed list.
 * Do not free individual items; do not inspect list->reserved.
 * After this call the struct is reset to a safe empty state.
 */
void whdtlv_string_list_free(WhdTlvStringList *list);

#ifdef __cplusplus
}
#endif

#endif /* WHDTLV_H */
