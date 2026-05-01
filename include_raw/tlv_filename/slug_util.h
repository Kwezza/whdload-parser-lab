/* slug_util.h - Helpers for generating normalized identity slugs (extension stripped)
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Produces a stable, case-normalized, separator-collapsed slug from a
 * WHDLoad package base filename (without path). The slug deliberately
 * excludes the archive extension so packaging changes (.lha -> .lzx)
 * do not alter identity keys.
 */
#ifndef TLV_FILENAME_SLUG_UTIL_H
#define TLV_FILENAME_SLUG_UTIL_H

#include <platform.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define TLV_SLUG_MAX 512 /* Hard cap for identity slug */

/* Generate slug from raw filename (may include extension). Rules:
 * 1. Remove final dot segment (.lha/.lzx/.zip etc.).
 * 2. Lowercase A-Z.
 * 3. Map any run of characters not in [a-z0-9] to single '_'.
 * 4. Trim leading/trailing '_'.
 * 5. Collapse consecutive '_' into one.
 * Returns length (>=1) or 0 on error/empty.
 */
size_t tlv_make_identity_slug(const char *filename, char *out_buf, size_t out_cap);

/* Hash = FNV-1a64 over: pack_type_id, '\0', slug bytes. */
uint64_t tlv_hash_identity_key(uint8_t pack_type_id, const char *slug);

#endif /* TLV_FILENAME_SLUG_UTIL_H */
/* End of Text */
