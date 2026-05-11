/* tlv_reader.h - TLV File Read-Back Declarations
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Internal header for TLV file read-back functions.  These are needed by
 * the staged filter loading code (filter_runtime.c) and are therefore kept
 * alive, but are not part of the public build-from-DAT API declared in
 * tlv_builder.h.
 *
 * Decision recorded 2026-05-11: read-back path retained because
 * filter_runtime.c uses tlv_read_record_with_metadata for snapshot loading.
 * See notes/backport_inventory.md §TLV read-back.
 *
 * tlv_read_metadata_map is file-local (static) in tlv_builder.c; it is not
 * declared here.
 */

#ifndef TLV_FILENAME_TLV_READER_H
#define TLV_FILENAME_TLV_READER_H

#pragma once

#include <platform.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/field_registry.h>

/**
 * @brief Read TLV record from file and reconstruct field registry
 * @param file Input file
 * @param record TLV record to populate
 * @param field_registry Output: reconstructed field registry (if metadata map present)
 * @return true if successful, false on error
 */
bool tlv_read_record_with_metadata(FILE *file,
                                   TLV_Record *record,
                                   FieldRegistry **field_registry);

/**
 * @brief Check if TLV file contains embedded metadata map
 * @param file Input file (will be rewound)
 * @return true if metadata map present, false otherwise
 */
bool tlv_has_metadata_map(FILE *file);

#endif /* TLV_FILENAME_TLV_READER_H */

/* End of Text */
