/* tlv_builder.h - TLV Record Builder with Dynamic Field Registry
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * TLV record building system with dynamic field registry integration.
 * Builds Type-Length-Value records using runtime field ID assignment
 * and embedded metadata maps for backward compatibility.
 *
 * Key Features:
 * - Dynamic field registry integration (no hardcoded field IDs)
 * - Embedded metadata maps in TLV files (type 0x01)
 * - Runtime field ID assignment for user extensibility
 * - Type-Length-Value encoding with proper field ordering
 * - Cross-platform compatibility (Amiga/Host)
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#ifndef TLV_FILENAME_TLV_BUILDER_H
#define TLV_FILENAME_TLV_BUILDER_H

#pragma once

#include <platform.h>
#include <tlv_filename/error_handling.h>
#include <tlv_filename/field_registry.h>

/*------------------------------------------------------------------------*/
/* TLV Record Types */

/**
 * @brief Single TLV entry (Type-Length-Value)
 */
typedef struct {
    uint8_t field_id;           /* Field ID from dynamic registry */
    uint16_t length;            /* Length of value data */
    uint8_t *value;             /* Pointer to value data */
} TLV_Entry;

/**
 * @brief Complete TLV record with multiple entries
 */
typedef struct TLV_Record {
    TLV_Entry *entries;         /* Array of TLV entries */
    uint32_t entry_count;       /* Number of entries */
    uint32_t capacity;          /* Allocated capacity */
    uint32_t total_size;        /* Total size in bytes */
} TLV_Record;

/**
 * @brief Processing summary for batch operations
 */
typedef struct {
    uint32_t total_processed;   /* Total number of items processed */
    uint32_t successful_count;  /* Number of successful operations */
    uint32_t error_count;       /* Number of failed operations */
} ProcessingSummary;

/*------------------------------------------------------------------------*/
/* Reserved TLV Types */

/* Reserved TLV types for system use */
#define TLV_TYPE_METADATA_MAP    0x01   /* Embedded metadata map */
#define TLV_TYPE_RECORD_COUNT    0x02   /* Number of records in file */
#define TLV_TYPE_FILE_VERSION    0x03   /* TLV file format version */
/* Current TLV file format version (increment on incompatible changes) */
#define TLV_FILE_VERSION         0x0001

/* Dynamic field range: 0x04-0xFF (matches field registry) */
#define TLV_TYPE_DYNAMIC_MIN     0x04
#define TLV_TYPE_DYNAMIC_MAX     0xFF

/*------------------------------------------------------------------------*/
/* TLV Record Management */

/**
 * @brief Initialize a new TLV record
 * @param record TLV record to initialize
 * @return true if successful, false on error
 */
bool tlv_record_init(TLV_Record *record);

/**
 * @brief Free TLV record and all associated memory
 * @param record TLV record to free
 */
void tlv_record_free(TLV_Record *record);

/**
 * @brief Add entry to TLV record using field name (dynamic registry lookup)
 * @param record TLV record to add to
 * @param field_registry Dynamic field registry for ID resolution
 * @param field_name Field name to add
 * @param value Pointer to value data
 * @param length Length of value data
 * @return true if successful, false on error
 */
bool tlv_record_add_field_by_name(TLV_Record *record,
                                 const FieldRegistry *field_registry,
                                 const char *field_name,
                                 const uint8_t *value,
                                 uint16_t length);

/**
 * @brief Add entry to TLV record using field ID directly
 * @param record TLV record to add to
 * @param field_id Field ID from dynamic registry
 * @param value Pointer to value data
 * @param length Length of value data
 * @return true if successful, false on error
 */
bool tlv_record_add_entry(TLV_Record *record,
                         uint8_t field_id,
                         const uint8_t *value,
                         uint16_t length);

/**
 * @brief Get entry from TLV record by field name
 * @param record TLV record to search
 * @param field_registry Dynamic field registry for ID resolution
 * @param field_name Field name to find
 * @return Pointer to TLV_Entry if found, NULL if not found
 */
const TLV_Entry *tlv_record_get_field_by_name(const TLV_Record *record,
                                             const FieldRegistry *field_registry,
                                             const char *field_name);

/**
 * @brief Get entry from TLV record by field ID
 * @param record TLV record to search
 * @param field_id Field ID to find
 * @return Pointer to TLV_Entry if found, NULL if not found
 */
const TLV_Entry *tlv_record_get_entry(const TLV_Record *record, uint8_t field_id);

/*------------------------------------------------------------------------*/
/* TLV File I/O */

/**
 * @brief Write TLV record to file with embedded metadata map
 * @param file Output file
 * @param record TLV record to write
 * @param field_registry Dynamic field registry for metadata map
 * @return true if successful, false on error
 */
bool tlv_write_record_with_metadata(FILE *file,
                                   const TLV_Record *record,
                                   const FieldRegistry *field_registry);

/**
 * @brief Write embedded metadata map to TLV file
 * @param file Output file
 * @param field_registry Dynamic field registry
 * @return true if successful, false on error
 */
bool tlv_write_metadata_map(FILE *file, const FieldRegistry *field_registry);

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

/**
 * @brief Read embedded metadata map from TLV file
 * @param file Input file
 * @param field_registry Field registry to populate
 * @return true if successful, false on error
 */
bool tlv_read_metadata_map(FILE *file, FieldRegistry *field_registry);

/*------------------------------------------------------------------------*/
/* Session Management */

/**
 * @brief Initialize TLV processing session with field registry and CSV cache
 * @param csv_folder_path Path to CSV definitions folder
 * @param pack_types_ini_path Path to pack_types.ini configuration
 * @return true if successful, false on error
 */
bool tlv_session_init(const char *csv_folder_path, const char *pack_types_ini_path);

/**
 * @brief Process batch of filenames and generate TLV records
 * @param filenames Array of filenames to process
 * @param filename_count Number of filenames
 * @param pack_type_id Pack type to use for processing
 * @param output_records Array to store generated TLV records (caller must allocate)
 * @param processing_summary Output: summary of processing results
 * @return true if successful, false on error
 */
bool tlv_session_process_batch(const char **filenames, uint32_t filename_count,
                              uint32_t pack_type_id, TLV_Record *output_records,
                              ProcessingSummary *processing_summary);

/**
 * @brief Finalize TLV processing session and cleanup resources
 */
void tlv_session_finalize(void);

/*------------------------------------------------------------------------*/
/* Legacy Compatibility */

/**
 * @brief Create TLV record from CSV data and configuration (legacy interface)
 * @param pack_name Name of the WHDLoad package
 * @param csv_dir Directory containing CSV files
 * @param pack_types_ini Path to pack_types.ini configuration
 * @return Pointer to TLV_Record on success, NULL on failure
 */
TLV_Record *tlv_builder_create_record(const char *pack_name,
                                      const char *csv_dir,
                                      const char *pack_types_ini);

/**
 * @brief Build TLV metadata from DAT XML file and CSV definitions (legacy interface)
 * @param dat_path Path to DAT XML file
 * @param csv_folder_path Path to CSV definitions folder
 * @param output_path Path to write TLV metadata file
 * @return true if successful, false on error
 */
bool build_tlv_from_dat(const char *dat_path, const char *csv_folder_path, const char *output_path);

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_TLV_BUILDER_H */

/* End of Text */
