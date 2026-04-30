/* embedded_metadata.h - Embedded Metadata Map System for TLV Files
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Embedded metadata map system that stores field definitions inside TLV files
 * as type 0x01 records. This ensures backward compatibility - each TLV file
 * contains its own field registry for decoding, independent of the current
 * system configuration.
 *
 * Key Features:
 * - TLV type 0x01 contains (field_id, field_name) pairs
 * - Self-documenting TLV files with embedded field definitions
 * - Backward compatibility when pack_types.ini changes
 * - Automatic metadata map generation during TLV creation
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#ifndef TLV_FILENAME_EMBEDDED_METADATA_H
#define TLV_FILENAME_EMBEDDED_METADATA_H

#pragma once

#include <platform.h>
#include <tlv_filename/field_registry.h>

/*------------------------------------------------------------------------*/
/* Metadata Map Types */

/**
 * @brief Single field mapping entry in embedded metadata map
 */
typedef struct {
    uint8_t field_id;           /* Runtime field ID */
    char field_name[32];        /* Field name */
} FieldMapping;

/**
 * @brief Embedded metadata map structure
 *
 * This structure represents the metadata map that gets embedded in TLV files
 * as type 0x01. It contains all field mappings needed to decode the file.
 */
typedef struct {
    uint32_t mapping_count;     /* Number of field mappings */
    FieldMapping *mappings;     /* Array of field mappings */
    uint32_t total_size;        /* Total size in bytes when serialized */
} EmbeddedMetadataMap;

/*------------------------------------------------------------------------*/
/* TLV Metadata Map API */

/**
 * @brief Write metadata map to TLV file as type 0x01
 * @param tlv_file Output TLV file handle
 * @param registry Field registry to embed
 * @return true if successful, false on error
 */
bool tlv_write_metadata_map(FILE *tlv_file, const FieldRegistry *registry);

/**
 * @brief Read metadata map from TLV file and reconstruct field registry
 * @param tlv_file Input TLV file handle
 * @param registry Field registry to populate
 * @return true if successful, false on error
 */
bool tlv_read_metadata_map(FILE *tlv_file, FieldRegistry *registry);

/**
 * @brief Check if TLV file contains embedded metadata map
 * @param tlv_file TLV file handle (positioned at start)
 * @return true if metadata map present, false otherwise
 */
bool tlv_has_metadata_map(FILE *tlv_file);

/*------------------------------------------------------------------------*/
/* Metadata Map Management */

/**
 * @brief Create embedded metadata map from field registry
 * @param registry Source field registry
 * @return Pointer to metadata map, NULL on error
 */
EmbeddedMetadataMap *create_metadata_map(const FieldRegistry *registry);

/**
 * @brief Free embedded metadata map and associated memory
 * @param metadata_map Metadata map to free
 */
void free_metadata_map(EmbeddedMetadataMap *metadata_map);

/**
 * @brief Calculate size of serialized metadata map
 * @param registry Field registry
 * @return Size in bytes when serialized
 */
uint32_t calculate_metadata_map_size(const FieldRegistry *registry);

/*------------------------------------------------------------------------*/
/* Metadata Map Validation */

/**
 * @brief Validate embedded metadata map for consistency
 * @param metadata_map Metadata map to validate
 * @return true if valid, false if corruption detected
 */
bool validate_metadata_map(const EmbeddedMetadataMap *metadata_map);

/**
 * @brief Compare two metadata maps for compatibility
 * @param map1 First metadata map
 * @param map2 Second metadata map
 * @return true if compatible, false if different field mappings
 */
bool metadata_maps_compatible(const EmbeddedMetadataMap *map1, const EmbeddedMetadataMap *map2);

/*------------------------------------------------------------------------*/
/* Reserved TLV Types */

/* Reserved TLV type for embedded metadata map */
#define TLV_TYPE_METADATA_MAP    0x01

/* Standard TLV data types */
#define TLV_TYPE_INTEGER         0x02
#define TLV_TYPE_STRING          0x03

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_EMBEDDED_METADATA_H */

/* End of Text */
