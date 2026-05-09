/* filtering/tlv_crc_validate.h - CSV CRC fingerprint validation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Compares CRC-32 fingerprints embedded in the TLV against the current
 * CSV files in defs_dir.  Must run before profile binding and scoring.
 *
 * Strict mode:   any missing, unreadable, or mismatched CSV aborts.
 * Warn-only mode: warnings are recorded but filtering may continue.
 */

#ifndef FILTERING_TLV_CRC_VALIDATE_H
#define FILTERING_TLV_CRC_VALIDATE_H

#include <platform.h>
#include <filtering/tlv_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Per-CSV result codes                                                   */

#define WHD_CRC_OK          0
#define WHD_CRC_MISSING     1
#define WHD_CRC_UNREADABLE  2
#define WHD_CRC_MISMATCH    3
#define WHD_CRC_NO_BLOCK    4  /* TLV has no embedded CRC fingerprint block */

/*------------------------------------------------------------------------*/
/* Validation summary                                                     */

typedef struct WhdCrcValidateResult {
    unsigned long ok_count;
    unsigned long missing_count;
    unsigned long unreadable_count;
    unsigned long mismatch_count;
    int           no_crc_block;   /* 1 if TLV had no fingerprint block */
} WhdCrcValidateResult;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * Validate all CSV fingerprints embedded in rt against current files in
 * defs_dir.  flags must be WHD_FILTER_CRC_STRICT or
 * WHD_FILTER_CRC_WARNONLY (defined in tlv_filter.h).
 *
 * In strict mode any failure returns WHD_FILTER_ERR_CSV_* immediately.
 * In warn-only mode the function returns WHD_FILTER_OK and records counts
 * in *out so the caller can surface them as warnings.
 *
 * out may be NULL if summary counts are not needed.
 */
int tlv_crc_validate(const TlvRuntime     *rt,
                     const char           *defs_dir,
                     unsigned int          flags,
                     WhdCrcValidateResult *out);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_CRC_VALIDATE_H */
/* End of Text */
