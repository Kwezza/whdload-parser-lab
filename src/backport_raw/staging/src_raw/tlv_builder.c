/* tlv_builder.c - TLV Record Builder with Dynamic Field Registry
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implementation of TLV record building system with dynamic field registry
 * integration. Builds Type-Length-Value records using runtime field ID
 * assignment and embedded metadata maps for backward compatibility.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#include <platform.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/filename_processor.h>
#include <tlv_filename/csv_cache.h>
#include <io/pack_types_loader.h>
#include <io/writeLog.h>
#include <string.h>
#include <stdio.h>

/*------------------------------------------------------------------------*/
/* Constants */

#define TLV_INITIAL_CAPACITY     16      /* Initial number of TLV entries */
#define TLV_GROWTH_FACTOR        2       /* Growth factor for dynamic expansion */
/* Increased to handle large aggregated runs across multiple packs */
#define TLV_MAX_ENTRIES          32768   /* Maximum entries per record */
#define TLV_MAX_VALUE_LENGTH     65535   /* Maximum length of a single value */

/*------------------------------------------------------------------------*/
/* Static Session Variables */

static FieldRegistry *session_field_registry = NULL;
static GlobalCSVManager session_csv_manager = {0};
static PackType *session_pack_types = NULL;
static size_t session_pack_count = 0;
static bool session_initialized = false;

/*------------------------------------------------------------------------*/
/* TLV Record Management */

/**
 * @brief Initialize a new TLV record
 */
bool tlv_record_init(TLV_Record *record) {
    if (!record) {
        return false;
    }

    /* Initialize record structure */
    record->entries = whd_malloc(TLV_INITIAL_CAPACITY * sizeof(TLV_Entry));
    if (!record->entries) {
        return false;
    }

    record->entry_count = 0;
    record->capacity = TLV_INITIAL_CAPACITY;
    record->total_size = 0;

    /* Clear the entries array */
    memset(record->entries, 0, TLV_INITIAL_CAPACITY * sizeof(TLV_Entry));

    return true;
}

/**
 * @brief Free TLV record and all associated memory
 */
void tlv_record_free(TLV_Record *record) {
    if (!record) {
        return;
    }

    /* Free all value data */
    for (uint32_t i = 0; i < record->entry_count; i++) {
        if (record->entries[i].value) {
            whd_free(record->entries[i].value);
            record->entries[i].value = NULL;
        }
    }

    /* Free entries array */
    if (record->entries) {
        whd_free(record->entries);
        record->entries = NULL;
    }

    /* Reset record */
    record->entry_count = 0;
    record->capacity = 0;
    record->total_size = 0;
}

/**
 * @brief Expand TLV record capacity
 */
static bool tlv_record_expand(TLV_Record *record) {
    if (!record || record->capacity >= TLV_MAX_ENTRIES) {
        return false;
    }

    /* Calculate new capacity */
    uint32_t new_capacity = record->capacity * TLV_GROWTH_FACTOR;
    if (new_capacity > TLV_MAX_ENTRIES) {
        new_capacity = TLV_MAX_ENTRIES;
    }

    /* Reallocate entries array */
    TLV_Entry *new_entries = whd_malloc(new_capacity * sizeof(TLV_Entry));
    if (!new_entries) {
        return false;
    }

    /* Copy existing entries */
    memcpy(new_entries, record->entries, record->entry_count * sizeof(TLV_Entry));

    /* Clear new entries */
    memset(new_entries + record->entry_count, 0,
           (new_capacity - record->entry_count) * sizeof(TLV_Entry));

    /* Replace old array */
    whd_free(record->entries);
    record->entries = new_entries;
    record->capacity = new_capacity;

    return true;
}

/**
 * @brief Add entry to TLV record using field ID directly
 */
bool tlv_record_add_entry(TLV_Record *record,
                         uint8_t field_id,
                         const uint8_t *value,
                         uint16_t length) {
    /* Length is uint16_t, max representable equals TLV_MAX_VALUE_LENGTH; no need to compare */
    if (!record || !value) {
        return false;
    }

    /* Preserve safety if project lowers TLV_MAX_VALUE_LENGTH in the future */
#if (TLV_MAX_VALUE_LENGTH < 65535)
    if ((unsigned)length > (unsigned)TLV_MAX_VALUE_LENGTH) {
        return false;
    }
#endif

    /* Check if we need to expand capacity */
    if (record->entry_count >= record->capacity) {
        if (!tlv_record_expand(record)) {
            return false;
        }
    }

    /* Allocate memory for value */
    uint8_t *value_copy = whd_malloc(length);
    if (!value_copy) {
        return false;
    }
    memcpy(value_copy, value, length);

    /* Add entry */
    TLV_Entry *entry = &record->entries[record->entry_count];
    entry->field_id = field_id;
    entry->length = length;
    entry->value = value_copy;

    record->entry_count++;
    record->total_size += 3 + length; /* 1 byte field_id + 2 bytes length + value */

    return true;
}

/**
 * @brief Add entry to TLV record using field name (dynamic registry lookup)
 */
bool tlv_record_add_field_by_name(TLV_Record *record,
                                 const FieldRegistry *field_registry,
                                 const char *field_name,
                                 const uint8_t *value,
                                 uint16_t length) {
    if (!record || !field_registry || !field_name || !value) {
        return false;
    }

    /* Look up field ID from registry */
    uint8_t field_id = field_registry_get_id(field_registry, field_name);
    if (field_id == 0) {
        /* Field not found in registry */
        return false;
    }

    /* Add entry using field ID */
    return tlv_record_add_entry(record, field_id, value, length);
}

/**
 * @brief Get entry from TLV record by field ID
 */
const TLV_Entry *tlv_record_get_entry(const TLV_Record *record, uint8_t field_id) {
    if (!record) {
        return NULL;
    }

    /* Linear search for field ID */
    for (uint32_t i = 0; i < record->entry_count; i++) {
        if (record->entries[i].field_id == field_id) {
            return &record->entries[i];
        }
    }

    return NULL;
}

/**
 * @brief Get entry from TLV record by field name
 */
const TLV_Entry *tlv_record_get_field_by_name(const TLV_Record *record,
                                             const FieldRegistry *field_registry,
                                             const char *field_name) {
    if (!record || !field_registry || !field_name) {
        return NULL;
    }

    /* Look up field ID from registry */
    uint8_t field_id = field_registry_get_id(field_registry, field_name);
    if (field_id == 0) {
        return NULL;
    }

    /* Get entry by field ID */
    return tlv_record_get_entry(record, field_id);
}

/*------------------------------------------------------------------------*/
/* TLV File I/O */

/**
 * @brief Write embedded metadata map to TLV file
 */
bool tlv_write_metadata_map(FILE *file, const FieldRegistry *field_registry) {
    if (!file || !field_registry) {
        return false;
    }

    /* Calculate metadata map size by iterating through field IDs */
    uint16_t map_size = 0;
    /* Note: field_registry_get_count() result is not required here */

    /* First pass: calculate size */
    for (uint16_t field_id = FIELD_ID_DYNAMIC_MIN; field_id <= FIELD_ID_DYNAMIC_MAX; field_id++) {
        const char *field_name = field_registry_get_name(field_registry, (uint8_t)field_id);
        if (field_name) {
            map_size += 1 + strlen(field_name) + 1; /* field_id + name + null terminator */
        }
    }

    if (map_size == 0) {
        return true; /* No fields to write */
    }

    /* Write metadata map header */
    uint8_t map_type = TLV_TYPE_METADATA_MAP;
    if (fwrite(&map_type, 1, 1, file) != 1) {
        return false;
    }
    if (fwrite(&map_size, 2, 1, file) != 1) {
        return false;
    }

    /* Second pass: write field mappings */
    for (uint16_t field_id = FIELD_ID_DYNAMIC_MIN; field_id <= FIELD_ID_DYNAMIC_MAX; field_id++) {
        const char *field_name = field_registry_get_name(field_registry, (uint8_t)field_id);
        if (field_name) {
            /* Write field ID */
            uint8_t field_id_byte = (uint8_t)field_id;
            if (fwrite(&field_id_byte, 1, 1, file) != 1) {
                return false;
            }

            /* Write field name with null terminator */
            size_t name_len = strlen(field_name) + 1;
            if (fwrite(field_name, 1, name_len, file) != name_len) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Write TLV record to file with embedded metadata map
 */
bool tlv_write_record_with_metadata(FILE *file,
                                   const TLV_Record *record,
                                   const FieldRegistry *field_registry) {
    if (!file || !record) {
        return false;
    }

    /* Write metadata map first if field registry provided */
    if (field_registry) {
        if (!tlv_write_metadata_map(file, field_registry)) {
            return false;
        }
    }

    /* Write each TLV entry */
    for (uint32_t i = 0; i < record->entry_count; i++) {
        const TLV_Entry *entry = &record->entries[i];

        /* Write field ID */
        if (fwrite(&entry->field_id, 1, 1, file) != 1) {
            return false;
        }

        /* Write length */
        if (fwrite(&entry->length, 2, 1, file) != 1) {
            return false;
        }

        /* Write value */
        if (entry->length > 0) {
            if (fwrite(entry->value, 1, entry->length, file) != entry->length) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Check if TLV file contains embedded metadata map
 */
bool tlv_has_metadata_map(FILE *file) {
    if (!file) {
        return false;
    }

    /* Save current position */
    long current_pos = ftell(file);

    /* Rewind and check first byte */
    rewind(file);
    uint8_t first_type;
    bool has_map = (fread(&first_type, 1, 1, file) == 1) &&
                   (first_type == TLV_TYPE_METADATA_MAP);

    /* Restore position */
    fseek(file, current_pos, SEEK_SET);

    return has_map;
}

/**
 * @brief Read embedded metadata map from TLV file
 */
bool tlv_read_metadata_map(FILE *file, FieldRegistry *field_registry) {
    if (!file || !field_registry) {
        return false;
    }

    /* Read metadata map header */
    uint8_t map_type;
    uint16_t map_size;

    if (fread(&map_type, 1, 1, file) != 1 || map_type != TLV_TYPE_METADATA_MAP) {
        return false;
    }
    if (fread(&map_size, 2, 1, file) != 1) {
        return false;
    }

    /* Read field mappings */
    uint16_t bytes_read = 0;
    while (bytes_read < map_size) {
        uint8_t field_id;
        char field_name[64];

        /* Read field ID */
        if (fread(&field_id, 1, 1, file) != 1) {
            return false;
        }
        bytes_read += 1;

        /* Read field name */
        size_t name_pos = 0;
        char c;
        do {
            if (fread(&c, 1, 1, file) != 1 || name_pos >= sizeof(field_name) - 1) {
                return false;
            }
            field_name[name_pos++] = c;
            bytes_read += 1;
        } while (c != '\0' && bytes_read < map_size);

        field_name[name_pos] = '\0';

        /* Add field to registry */
        if (!field_registry_add_field(field_registry, field_name, field_id)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Read TLV record from file and reconstruct field registry
 */
bool tlv_read_record_with_metadata(FILE *file,
                                  TLV_Record *record,
                                  FieldRegistry **field_registry) {
    if (!file || !record) {
        return false;
    }

    /* Initialize record */
    if (!tlv_record_init(record)) {
        return false;
    }

    /* Check for metadata map */
    if (tlv_has_metadata_map(file)) {
        if (field_registry) {
            *field_registry = field_registry_alloc();
            if (!*field_registry || !tlv_read_metadata_map(file, *field_registry)) {
                if (*field_registry) {
                    field_registry_free(*field_registry);
                    *field_registry = NULL;
                }
                tlv_record_free(record);
                return false;
            }
        } else {
            /* Skip metadata map */
            uint16_t map_size;
            fseek(file, 1, SEEK_CUR); /* Skip type byte */
            if (fread(&map_size, 2, 1, file) != 1) {
                tlv_record_free(record);
                return false;
            }
            fseek(file, map_size, SEEK_CUR);
        }
    }

    /* Read TLV entries */
    while (!feof(file)) {
        uint8_t field_id;
        uint16_t length;

        /* Read field ID */
        if (fread(&field_id, 1, 1, file) != 1) {
            if (feof(file)) break; /* End of file reached */
            tlv_record_free(record);
            return false;
        }

        /* Read length */
        if (fread(&length, 2, 1, file) != 1) {
            tlv_record_free(record);
            return false;
        }

        /* Read value */
        uint8_t *value = NULL;
        if (length > 0) {
            value = whd_malloc(length);
            if (!value || fread(value, 1, length, file) != length) {
                if (value) whd_free(value);
                tlv_record_free(record);
                return false;
            }
        }

        /* Add entry to record */
        if (!tlv_record_add_entry(record, field_id, value ? value : (const uint8_t*)"", length)) {
            if (value) whd_free(value);
            tlv_record_free(record);
            return false;
        }

        if (value) whd_free(value);
    }

    return true;
}

/*------------------------------------------------------------------------*/
/* Session Management */

/**
 * @brief Initialize TLV processing session with field registry and CSV cache
 */
bool tlv_session_init(const char *csv_folder_path, const char *pack_types_ini_path) {
    if (!csv_folder_path || !pack_types_ini_path) {
        return false;
    }

    /* Cleanup any existing session */
    if (session_initialized) {
        tlv_session_finalize();
    }

    /* Initialize field registry */
    session_field_registry = field_registry_alloc();
    if (!session_field_registry) {
        return false;
    }

    /* Build field registry from pack types configuration */
    if (!build_field_registry_from_ini(session_field_registry, pack_types_ini_path)) {
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        return false;
    }

    /* Initialize CSV cache manager with required parameters */
    if (!csv_cache_manager_init(&session_csv_manager, NULL, csv_folder_path)) {
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        return false;
    }

    /* Load pack types configuration */
    session_pack_types = load_pack_types(pack_types_ini_path, &session_pack_count);
    if (!session_pack_types) {
        csv_cache_manager_cleanup(&session_csv_manager);
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        return false;
    }

    session_initialized = true;
    return true;
}

/**
 * @brief Process batch of filenames and generate TLV records
 */
bool tlv_session_process_batch(const char **filenames, uint32_t filename_count,
                              uint32_t pack_type_id, TLV_Record *output_records,
                              ProcessingSummary *processing_summary) {
    if (!filenames || !output_records || !processing_summary || !session_initialized) {
        return false;
    }

    /* Validate pack type ID */
    if (pack_type_id >= (uint32_t)session_pack_count) {
        return false;
    }

    /* Initialize processing summary */
    processing_summary->total_processed = 0;
    processing_summary->successful_count = 0;
    processing_summary->error_count = 0;

    /* Get pack type configuration */
    const PackType *pack_info = &session_pack_types[pack_type_id];

    /* Process each filename */
    for (uint32_t i = 0; i < filename_count; i++) {
        ProcessingError error;

        /* Initialize output record */
        if (!tlv_record_init(&output_records[i])) {
            processing_summary->error_count++;
            processing_summary->total_processed++;
            continue;
        }

        /* Process filename using our completed orchestrator */
        ProcessingResult result = tlv_process_filename_orchestrator(
            filenames[i], pack_info, session_field_registry,
            &session_csv_manager, &output_records[i], &error);

        if (result == PROCESSING_SUCCESS) {
            processing_summary->successful_count++;
        } else {
            processing_summary->error_count++;
            /* Log error for debugging */
#if PLATFORM_AMIGA
            append_to_log("TLV processing failed for '%s': %s",
                         filenames[i], error.error_message);
#endif
        }

        processing_summary->total_processed++;
    }

    return true;
}

/**
 * @brief Finalize TLV processing session and cleanup resources
 */
void tlv_session_finalize(void) {
    if (!session_initialized) {
        return;
    }

    /* Cleanup CSV cache manager */
    csv_cache_manager_cleanup(&session_csv_manager);

    /* Cleanup field registry */
    if (session_field_registry) {
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
    }

    /* Cleanup pack types */
    if (session_pack_types) {
        /* Free pack types (assuming there's a cleanup function) */
        /* Note: This would need to be implemented based on pack types loader */
        session_pack_types = NULL;
        session_pack_count = 0;
    }

    session_initialized = false;
}

/*------------------------------------------------------------------------*/
/* Legacy Compatibility */

/**
 * @brief Create TLV record from CSV data and configuration (legacy interface)
 */
TLV_Record *tlv_builder_create_record(const char *pack_name,
                                      const char *csv_dir,
                                      const char *pack_types_ini) {
    if (!pack_name || !csv_dir || !pack_types_ini) {
        return NULL;
    }

    /* Initialize session if not already done */
    if (!session_initialized) {
        if (!tlv_session_init(csv_dir, pack_types_ini)) {
            return NULL;
        }
    }

    /* Allocate TLV record */
    TLV_Record *record = whd_malloc(sizeof(TLV_Record));
    if (!record) {
        return NULL;
    }

    /* Initialize record */
    if (!tlv_record_init(record)) {
        whd_free(record);
        return NULL;
    }

    /* Process single filename (assume pack type 0 - Games) */
    ProcessingError error;
    if (session_pack_count > 0) {
        ProcessingResult result = tlv_process_filename_orchestrator(
            pack_name, &session_pack_types[0], session_field_registry,
            &session_csv_manager, record, &error);

        if (result != PROCESSING_SUCCESS) {
            tlv_record_free(record);
            whd_free(record);
            return NULL;
        }
    }

    return record;
}

/**
 * @brief Build TLV metadata from DAT XML file and CSV definitions (legacy interface)
 */
bool build_tlv_from_dat(const char *dat_path, const char *csv_folder_path, const char *output_path) {
    /* This would be a more complex implementation involving DAT file parsing */
    /* For now, return false to indicate not implemented */
    /* parameters currently unused */
#if PLATFORM_HOST
    WHD_UNUSED(dat_path);
    WHD_UNUSED(csv_folder_path);
    WHD_UNUSED(output_path);
#endif
    return false;
}

/*------------------------------------------------------------------------*/

/* End of Text */
