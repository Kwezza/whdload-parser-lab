/* include_raw/group_util.h - Canonical group-name derivation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Single source of truth for deriving the canonical group name from a
 * display_name string.  Used by:
 *   - dat_to_tlv builder   (to assign group_id during TLV construction)
 *   - filtering runtime    (fallback grouping for old TLVs without group_id)
 *
 * The derivation rule is:
 *   1. Find the first "_v<digit>" marker (case-insensitive 'v').
 *   2. The canonical group name is the text before that marker.
 *   3. If no such marker exists, the full display_name is used as-is.
 *   4. An empty result is never emitted; if derivation yields an empty
 *      string (e.g. the name starts with "_v1"), fall back to the full
 *      display_name.
 *
 * Example:
 *   "AlienBreed2_v1.0_AGA_En"  ->  "AlienBreed2"
 *   "ActionFighter"             ->  "ActionFighter"
 *   "V10_AGA"                   ->  "V10_AGA"   (no _v<digit> pattern)
 *
 * C89-compatible; vbcc-safe.
 */

#ifndef GROUP_UTIL_H
#define GROUP_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * whdtlv_derive_group_name
 *
 * Writes the canonical group name for display_name into out[0..out_size-1]
 * and NUL-terminates it.  Returns the number of characters written
 * (excluding the NUL terminator), which is 0 only when display_name is
 * NULL or empty.
 *
 * out_size must be >= 1.  The result is always NUL-terminated if out_size
 * >= 1 and out != NULL.
 *
 * No heap allocation is performed.
 */
unsigned long whdtlv_derive_group_name(const char    *display_name,
                                char          *out,
                                unsigned long  out_size);

#ifdef __cplusplus
}
#endif

#endif /* GROUP_UTIL_H */
