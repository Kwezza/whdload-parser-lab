/* field_registry.h - Dynamic Field Registry for TLV Filename Processing
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Dynamic field registry system that assigns field IDs at runtime based on
 * pack_types.ini configuration. This enables user extensibility without
 * recompilation - users can add new fields and CSV files dynamically.
 *
 * Key Features:
 * - Runtime field ID assignment (0x04-0xFF range, 252 field limit)
 * - Field name to CSV filename mapping
 * - Embedded metadata maps for backward compatibility
 * - Session-based fresh assignment (no persistent IDs)
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#ifndef TLV_FILENAME_FIELD_REGISTRY_H
#define TLV_FILENAME_FIELD_REGISTRY_H

#pragma once

#include <platform.h>

/*------------------------------------------------------------------------*/
/* Forward Declarations */

/* Include headers to avoid conflicts */
#include <tlv_filename/error_handling.h>
#include <io/pack_types_loader.h>

/*------------------------------------------------------------------------*/
/* Core Types */

/**
 * @brief Single field definition in the dynamic registry
 */
typedef struct {
    uint8_t field_id;           /* Runtime-assigned ID (0x04-0xFF) */
    char field_name[32];        /* Field name from pack_types.ini */
    char csv_filename[64];      /* Corresponding CSV filename */
    bool is_active;             /* Whether this field is currently assigned */
    bool allow_multiple;        /* Whether this field can appear multiple times in one record */
    /* Prescan configuration (from [FieldAttributes]) */
    bool prescan_enabled;       /* Whether this field participates in prescan */
    uint16_t prescan_order;     /* Order priority (lower first) */
    bool prescan_multi_token;   /* Whether multi-token joined matches are attempted */
    bool prescan_remove;        /* Remove matched span from filename */
    bool prescan_allow_multiple;/* Allow multiple matches during prescan (defaults to allow_multiple) */
    char prescan_csv_override[64]; /* Optional override CSV base for prescan lookups */
    /* Default fallback semantics (implicit tokens when variant lacks field) */
    bool has_default;           /* True if CSV row marked with trailing 'default' column */
    uint16_t default_token_id;  /* Numeric ID from CSV for default token (0 if none) */
} FieldDefinition;

/**
 * @brief Dynamic field registry with runtime ID assignment
 *
 * This structure manages the mapping between field names (from pack_types.ini)
 * and runtime-assigned field IDs. The registry is built fresh each session
 * from the configuration file, enabling user extensibility.
 */
typedef struct {
    FieldDefinition *fields;    /* Array of field definitions */
    uint32_t field_count;       /* Number of registered fields */
    uint32_t max_fields;        /* Maximum fields (252 for 0x04-0xFF range) */
    uint8_t next_field_id;      /* Next available field ID */
    bool initialized;           /* Whether registry is initialized */
} FieldRegistry;

/*------------------------------------------------------------------------*/
/* Registry Management */

/**
 * @brief Allocate and initialize a new field registry
 * @return Pointer to new field registry, NULL on error
 */
FieldRegistry *field_registry_alloc(void);

/**
 * @brief Free field registry and all associated memory
 * @param registry Field registry to free
 */
void field_registry_free(FieldRegistry *registry);

/**
 * @brief Build field registry from pack_types.ini configuration
 * @param registry Field registry to populate
 * @param pack_types_ini_path Path to pack_types.ini file
 * @return true if successful, false on error
 */
bool build_field_registry_from_ini(FieldRegistry *registry, const char *pack_types_ini_path);

/**
 * @brief Build field registry from pack types array
 * @param registry Field registry to populate
 * @param pack_types Array of pack type definitions
 * @param pack_count Number of pack types in array
 * @param error Error context for reporting issues
 * @return PROCESSING_SUCCESS if successful, error code otherwise
 */
ProcessingResult build_field_registry_from_pack_types(FieldRegistry *registry,
                                                      const PackType *pack_types,
                                                      size_t pack_count,
                                                      ProcessingError *error);

/*------------------------------------------------------------------------*/
/* Field Resolution */

/**
 * @brief Get runtime field ID for a field name
 * @param registry Field registry
 * @param field_name Field name to look up
 * @return Field ID if found, 0 if not found
 */
uint8_t field_registry_get_id(const FieldRegistry *registry, const char *field_name);

/**
 * @brief Get field name for a runtime field ID
 * @param registry Field registry
 * @param field_id Field ID to look up
 * @return Field name if found, NULL if not found
 */
const char *field_registry_get_name(const FieldRegistry *registry, uint8_t field_id);

/**
 * @brief Get CSV filename for a field name
 * @param registry Field registry
 * @param field_name Field name to look up
 * @return CSV filename if found, NULL if not found
 */
const char *get_csv_filename_for_field(const FieldRegistry *registry, const char *field_name);

/**
 * @brief Add field to registry with specific field ID (for metadata map reconstruction)
 * @param registry Field registry
 * @param field_name Field name to add
 * @param field_id Specific field ID to assign
 * @return true if successful, false on error
 */
bool field_registry_add_field(FieldRegistry *registry, const char *field_name, uint8_t field_id);

/*------------------------------------------------------------------------*/
/* Registry Validation */

/**
 * @brief Check if field registry has available field IDs
 * @param registry Field registry
 * @return true if IDs available, false if approaching limit
 */
bool field_registry_has_available_ids(const FieldRegistry *registry);

/**
 * @brief Get count of registered fields
 * @param registry Field registry
 * @return Number of registered fields
 */
uint32_t field_registry_get_count(const FieldRegistry *registry);

/**
 * @brief Validate field registry for potential issues
 * @param registry Field registry
 * @return true if valid, false if issues detected
 */
bool field_registry_validate(const FieldRegistry *registry);

/*------------------------------------------------------------------------*/
/* Field Attributes (INI-driven) */

/**
 * @brief Set allow_multiple flag for a field by name
 * @param registry Field registry
 * @param field_name Field name to update
 * @param allow Whether multiple values are allowed
 * @return true if field found and updated, false otherwise
 */
bool field_registry_set_allow_multiple(FieldRegistry *registry, const char *field_name, bool allow);

/**
 * @brief Get allow_multiple flag for a field by name
 * @param registry Field registry
 * @param field_name Field name to query
 * @return true if multiple values are allowed, false otherwise
 */
bool field_registry_get_allow_multiple(const FieldRegistry *registry, const char *field_name);

/*------------------------------------------------------------------------*/
/* Prescan configuration accessors */

/**
 * @brief Prescan configuration bundle for a field
 */
typedef struct {
    const char *field_name;     /* Field name */
    const char *csv_base;       /* CSV base name to use (without .csv) */
    uint8_t field_id;           /* Runtime field ID */
    uint16_t order;             /* Prescan order */
    bool enabled;               /* Prescan enabled */
    bool multi_token;           /* Try windows > 1 */
    bool remove_from_filename;  /* Remove matched span */
    bool allow_multiple;        /* Allow multiple matches */
} FieldPrescanConfig;

/**
 * @brief Get prescan configuration for a single field
 * @param registry Field registry
 * @param field_name Field name to query
 * @param out_cfg Output config (filled if field exists)
 * @return true if field exists and has prescan settings available
 */
bool field_registry_get_prescan_config(const FieldRegistry *registry,
                                       const char *field_name,
                                       FieldPrescanConfig *out_cfg);

/**
 * @brief List all fields with prescan enabled
 * @param registry Field registry
 * @param out Array of FieldPrescanConfig items to fill
 * @param max_items Capacity of out array
 * @return Number of items written (<= max_items)
 */
uint32_t field_registry_list_prescan_fields(const FieldRegistry *registry,
                                            FieldPrescanConfig *out,
                                            uint32_t max_items);

/*------------------------------------------------------------------------*/
/* Default Token Accessors */

/**
 * @brief Set default token id for a field
 * @param registry Field registry
 * @param field_name Field name
 * @param token_id CSV numeric id to use as default
 * @return true if field found and updated
 */
bool field_registry_set_default(FieldRegistry *registry, const char *field_name, uint16_t token_id);

/**
 * @brief Get default token id for a field id
 * @param registry Field registry
 * @param field_id Runtime field id
 * @param has_default Optional out flag (may be NULL)
 * @return token id if present, 0 if none
 */
uint16_t field_registry_get_default_token_id(const FieldRegistry *registry, uint8_t field_id, bool *has_default);

/*------------------------------------------------------------------------*/
/* Reserved Field IDs */

/* Reserved range: 0x00-0x03 for system use */
#define FIELD_ID_RESERVED_MIN    0x00
#define FIELD_ID_RESERVED_MAX    0x03

/* Dynamic range: 0x04-0xFF for user fields (252 fields maximum) */
#define FIELD_ID_DYNAMIC_MIN     0x04
#define FIELD_ID_DYNAMIC_MAX     0xFF
#define FIELD_ID_DYNAMIC_COUNT   (FIELD_ID_DYNAMIC_MAX - FIELD_ID_DYNAMIC_MIN + 1)

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_FIELD_REGISTRY_H */

/* End of Text */
