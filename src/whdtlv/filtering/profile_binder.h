/* filtering/profile_binder.h - Self-contained .profile loader and TLV field binder
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Parses a `.profile` INI file and binds each filter field to a real TLV
 * field ID from the TlvRuntime field map.  Token values (include / exclude
 * lists) are resolved against the corresponding CSV definition file in
 * defs_dir, falling back to an FNV-1a 8-bit hash when the CSV row cannot
 * be found (same fallback the TLV builder uses, so both sides hash identically).
 *
 * This module has no dependency on the old pipeline headers (FieldRegistry,
 * GlobalCSVManager, variant_index, prefs, logging) — it uses only the
 * filtering subsystem types and standard C library functions.
 *
 * C89-compatible; vbcc-safe.
 */

#ifndef FILTERING_PROFILE_BINDER_H
#define FILTERING_PROFILE_BINDER_H

#include "platform.h"
#include "whdtlv/filtering/tlv_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Limits                                                                 */

#define PB_MAX_FIELDS   16   /* max filter fields per profile            */
#define PB_MAX_TOKENS   32   /* max tokens per include or exclude list   */

/* Selection-bucket limits (hard caps; violations are errors, not truncation) */
#define FP_MAX_BUCKET_FIELDS    4  /* max fields per profile that may use "/" */
#define FP_MAX_BUCKETS_FIELD    8  /* max "/"-separated buckets per include list */
#define FP_MAX_SELECTION_LANES 32  /* max generated lanes (Cartesian product)  */

/*------------------------------------------------------------------------*/
/* One include selection bucket                                           */

typedef struct WhdIncludeBucket {
    uint8_t start;  /* index of first token in include_ids[]  */
    uint8_t count;  /* number of tokens in this bucket        */
} WhdIncludeBucket;

/*------------------------------------------------------------------------*/
/* One bound field                                                        */

typedef struct WhdBoundField {
    uint8_t  tlv_field_id;              /* field ID from TlvRuntime field map */
    char     field_name[32];            /* e.g. "chipset"                     */
    uint8_t  weight;                    /* scoring weight 0-255               */
    uint8_t  include_count;
    uint8_t  exclude_count;
    uint8_t  rank_by_id[256];           /* rank at position (id & 0xFF);
                                           0xFF = not in include list         */
    uint16_t         include_ids[PB_MAX_TOKENS];
    uint16_t         exclude_ids[PB_MAX_TOKENS];
    WhdIncludeBucket buckets[FP_MAX_BUCKETS_FIELD]; /* slash-bucket metadata */
    uint8_t          bucket_count;      /* 0=empty include, 1=comma-only, N=slash */
    int              has_default;       /* 1 if CSV defines a default row     */
    uint16_t         default_token_id;  /* numeric ID of the default token    */
} WhdBoundField;

/*------------------------------------------------------------------------*/
/* Bound profile                                                          */

typedef struct WhdBoundProfile {
    char          id[64];
    char          name[128];
    uint32_t      version;
    WhdBoundField fields[PB_MAX_FIELDS];
    uint8_t       field_count;
    int           had_warnings;
    int           debug_enabled;
} WhdBoundProfile;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Load and bind a .profile file.
 *   path     - path to the .profile file
 *   rt       - loaded TlvRuntime (used to resolve field names to IDs)
 *   defs_dir - directory containing the CSV definition files
 *   out      - caller-allocated WhdBoundProfile to fill
 *
 * Returns WHD_FILTER_OK on success, or a negative WHD_FILTER_ERR_* code. */
int whd_profile_load(const char      *path,
                     const TlvRuntime *rt,
                     const char      *defs_dir,
                     WhdBoundProfile *out);

/* Print a human-readable dump of the bound profile (harness use only). */
void whd_profile_dump(const WhdBoundProfile *p);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_PROFILE_BINDER_H */
/* End of Text */
