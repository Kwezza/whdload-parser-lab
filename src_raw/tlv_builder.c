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
#include <tlv_filename/tlv_profile.h>
#include <tlv_filename/filename_processor.h>
#include <tlv_filename/csv_cache.h>
#include <tlv_filename/embedded_metadata.h>
#include <io/pack_types_loader.h>
#include <io/writeLog.h>
#include <utils/prettify.h>
#include <group_util.h>
#include <string.h>
#include <stdio.h>

#ifdef PLATFORM_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#endif

/*------------------------------------------------------------------------*/
/* Static function forward declarations */
static bool tlv_write_group_map(FILE *file);
static bool tlv_read_metadata_map(FILE *file, FieldRegistry *field_registry);

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
/* Internal group-map state                                               */

#define GROUP_MAP_NAME_MAX    128u
#define GROUP_MAP_INITIAL_CAP 512u

typedef struct {
    char     name[GROUP_MAP_NAME_MAX];
    uint16_t id;
} GroupMapEntry;

typedef struct {
    GroupMapEntry *entries;
    uint32_t       count;
    uint32_t       capacity;
    uint16_t       next_id;  /* 1-based; 0 is sentinel "not present" */
} GroupMap;

static GroupMap session_group_map;

static void group_map_free_internal(GroupMap *gm)
{
    if (!gm) {
        return;
    }
    if (gm->entries) {
        whd_free(gm->entries);
        gm->entries = NULL;
    }
    gm->count    = 0u;
    gm->capacity = 0u;
    gm->next_id  = 1u;
}

static bool group_map_init_internal(GroupMap *gm)
{
    group_map_free_internal(gm);
    gm->entries = (GroupMapEntry *)whd_malloc(
        GROUP_MAP_INITIAL_CAP * sizeof(GroupMapEntry));
    if (!gm->entries) {
        return false;
    }
    gm->capacity = GROUP_MAP_INITIAL_CAP;
    gm->next_id  = 1u;
    return true;
}

/* Returns 0 on OOM or group_id overflow; otherwise a 1-based group_id. */
static uint16_t group_map_get_or_insert(GroupMap *gm, const char *name)
{
    GroupMapEntry *entry;
    uint32_t       i;

    if (!gm || !name || name[0] == '\0') {
        return 0u;
    }

    for (i = 0u; i < gm->count; i++) {
        if (strcmp(gm->entries[i].name, name) == 0) {
            return gm->entries[i].id;
        }
    }

    /* Overflow guard: next_id wraps to 0 after 65535. */
    if (gm->next_id == 0u) {
        fprintf(stderr,
                "ERROR: group_id overflow: more than 65535 distinct groups.\n");
        whdtlv_log_append("ERROR: group_id overflow: more than 65535 distinct groups.");
        return 0u;
    }

    /* Grow if needed. */
    if (gm->count >= gm->capacity) {
        uint32_t       new_cap      = gm->capacity * 2u;
        GroupMapEntry *new_entries  =
            (GroupMapEntry *)whd_malloc(new_cap * sizeof(GroupMapEntry));
        if (!new_entries) {
            return 0u;
        }
        memcpy(new_entries, gm->entries, gm->count * sizeof(GroupMapEntry));
        whd_free(gm->entries);
        gm->entries  = new_entries;
        gm->capacity = new_cap;
    }

    entry = &gm->entries[gm->count];
    strncpy(entry->name, name, GROUP_MAP_NAME_MAX - 1u);
    entry->name[GROUP_MAP_NAME_MAX - 1u] = '\0';
    entry->id = gm->next_id;
    gm->count++;
    gm->next_id++;
    return entry->id;
}

#ifdef PLATFORM_AMIGA
#define TLV_HEARTBEAT_INTERVAL_TICKS (2UL * 50UL)

typedef struct BatchHeartbeat {
    struct DateStamp last_stamp;
    bool initialized;
} BatchHeartbeat;

static unsigned long heartbeat_tick_value(const struct DateStamp *stamp)
{
    if (!stamp) {
        return 0;
    }

    return ((unsigned long)stamp->ds_Days * 24UL * 60UL * 60UL * 50UL) +
           ((unsigned long)stamp->ds_Minute * 60UL * 50UL) +
           (unsigned long)stamp->ds_Tick;
}

static unsigned long heartbeat_elapsed_ticks(const struct DateStamp *start,
                                             const struct DateStamp *end)
{
    unsigned long start_ticks;
    unsigned long end_ticks;

    if (!start || !end) {
        return 0;
    }

    start_ticks = heartbeat_tick_value(start);
    end_ticks = heartbeat_tick_value(end);
    if (end_ticks < start_ticks) {
        return 0;
    }

    return end_ticks - start_ticks;
}

static void emit_batch_heartbeat(BatchHeartbeat *heartbeat,
                                 const char *filename,
                                 uint32_t current_index,
                                 uint32_t total_count,
                                 const ProcessingSummary *summary,
                                 bool force_emit)
{
    struct DateStamp now;

    if (!heartbeat || !filename || !summary) {
        return;
    }

    DateStamp(&now);
    if (!heartbeat->initialized) {
        heartbeat->last_stamp = now;
        heartbeat->initialized = true;
    }

    if (!force_emit &&
        heartbeat_elapsed_ticks(&heartbeat->last_stamp, &now) < TLV_HEARTBEAT_INTERVAL_TICKS) {
        return;
    }

    heartbeat->last_stamp = now;
    printf("[heartbeat] %lu/%lu ok=%lu err=%lu current=%-.80s\n",
           (unsigned long)current_index,
           (unsigned long)total_count,
           (unsigned long)summary->successful_count,
           (unsigned long)summary->error_count,
           filename);
    fflush(stdout);
}
#endif

static bool pack_field_uses_generic_csv_match(const char *field_name)
{
    if (!field_name || field_name[0] == '\0') {
        return false;
    }

    if (strcmp(field_name, "version") == 0 ||
        strcmp(field_name, "language") == 0 ||
        strcmp(field_name, "sps") == 0 ||
        strcmp(field_name, "contributors") == 0 ||
        strcmp(field_name, "archive_info") == 0) {
        return false;
    }

    return true;
}

/* Step C: corpus-derived hit order (descending). Any field not listed
 * gets pushed to the end so frequently-matching fields are tested first. */
static const char * const s_field_priority_order[] = {
    "chipset",        /* 385 */
    "video",          /* 241 */
    "memory",         /* 147 */
    "disks",          /*  44 */
    "software_houses",/*  42 */
    "media",          /*  19 */
    "cover_disks",    /*   8 */
    "crack_groups",   /*   4 */
    "compilations",   /*   3 */
    NULL
};

static uint32_t field_priority_index(const char *field_name)
{
    uint32_t i = 0;
    while (s_field_priority_order[i] != NULL) {
        if (strcmp(s_field_priority_order[i], field_name) == 0) {
            return i;
        }
        i++;
    }
    return i; /* unlisted fields sort to end */
}

static PackFieldMatcher *build_pack_field_matchers(const PackType *pack_info,
                                                   const FieldRegistry *field_registry,
                                                   GlobalCSVManager *csv_manager,
                                                   uint32_t *matcher_count)
{
    PackFieldMatcher *matchers;
    uint32_t count;
    uint32_t index;

    if (matcher_count) {
        *matcher_count = 0;
    }

    if (!pack_info || !field_registry || !matcher_count ||
        !pack_info->field_list || pack_info->num_fields == 0) {
        return NULL;
    }

    count = (uint32_t)pack_info->num_fields;
    matchers = whd_malloc(count * sizeof(*matchers));
    if (!matchers) {
        return NULL;
    }

    for (index = 0; index < count; index++) {
        const char *field_name = pack_info->field_list[index];
        matchers[index].field_name = field_name;
        matchers[index].csv_name = field_registry_get_csv_basename(field_registry, field_name);
        matchers[index].field_id = field_registry_get_id(field_registry, field_name);
        matchers[index].resolved_cache = NULL;
        matchers[index].generic_csv_match_enabled = pack_field_uses_generic_csv_match(field_name);
        if (csv_manager->cache_enabled && matchers[index].csv_name && matchers[index].csv_name[0] != '\0') {
            matchers[index].resolved_cache = csv_cache_get_or_load(csv_manager, matchers[index].csv_name);
        }
        /* If caching is enabled but the CSV still couldn't be loaded (file absent), any
         * slow-path lookup for this field will also return 0.  Disable generic CSV match
         * now so the hot loop never wastes time on a guaranteed miss. */
        if (matchers[index].generic_csv_match_enabled &&
            matchers[index].csv_name && matchers[index].csv_name[0] != '\0' &&
            csv_manager->cache_enabled &&
            matchers[index].resolved_cache == NULL) {
            matchers[index].generic_csv_match_enabled = false;
        }
    }

    /* Step C: insertion-sort matchers by ascending priority index so
     * the most frequently-matching fields are tested first at runtime. */
    {
        uint32_t i, j;
        for (i = 1; i < count; i++) {
            PackFieldMatcher key = matchers[i];
            uint32_t pri = field_priority_index(key.field_name);
            j = i;
            while (j > 0 && field_priority_index(matchers[j - 1].field_name) > pri) {
                matchers[j] = matchers[j - 1];
                j--;
            }
            matchers[j] = key;
        }
    }

    *matcher_count = count;
    return matchers;
}

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
    bool success;
    TLV_PROFILE_SCOPE(profile_stamp);

    /* Length is uint16_t, max representable equals TLV_MAX_VALUE_LENGTH; no need to compare */
    if (!record || !value) {
        return false;
    }

    TLV_PROFILE_START(profile_stamp);
    success = false;

    /* Preserve safety if project lowers TLV_MAX_VALUE_LENGTH in the future */
#if (TLV_MAX_VALUE_LENGTH < 65535)
    if ((unsigned)length > (unsigned)TLV_MAX_VALUE_LENGTH) {
        goto done;
    }
#endif

    /* Check if we need to expand capacity */
    if (record->entry_count >= record->capacity) {
        if (!tlv_record_expand(record)) {
            goto done;
        }
    }

    /* Allocate memory for value */
    uint8_t *value_copy = whd_malloc(length);
    if (!value_copy) {
        goto done;
    }
    memcpy(value_copy, value, length);

    /* Add entry */
    TLV_Entry *entry = &record->entries[record->entry_count];
    entry->field_id = field_id;
    entry->length = length;
    entry->value = value_copy;

    record->entry_count++;
    record->total_size += 3 + length; /* 1 byte field_id + 2 bytes length + value */

    success = true;

done:
    TLV_PROFILE_END(TLV_PROFILE_SECTION_TLV_ADD_ENTRY, profile_stamp);

    return success;
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
static const TLV_Entry *tlv_record_get_entry(const TLV_Record *record, uint8_t field_id) {
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
static bool tlv_write_metadata_map(FILE *file, const FieldRegistry *field_registry) {
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
 * @brief Write a type 0x04 CSV fingerprint record to a TLV file.
 *
 * Wire format:
 *   [1 byte : type = 0x04]
 *   [2 bytes: payload size, little-endian]
 *   [2 bytes: entry count, little-endian]
 *   per entry:
 *     [N bytes: null-terminated csv_name]
 *     [4 bytes: crc32, little-endian]
 */
static bool tlv_write_csv_fingerprints(FILE *file, const GlobalCSVManager *manager)
{
    uint32_t i;
    uint16_t payload_size;
    uint16_t count;
    uint8_t  rec_type;

    if (!file || !manager) {
        return false;
    }

    /* Only write if there are loaded caches */
    if (manager->cache_count == 0) {
        return true;
    }

    /* Calculate payload: 2 bytes count + per-entry (strlen(csv_name)+1 + 4) */
    payload_size = 2; /* count field */
    for (i = 0; i < manager->cache_count; i++) {
        const char *name = manager->caches[i].csv_name;
        if (!name) {
            continue;
        }
        payload_size += (uint16_t)(strlen(name) + 1 + 4);
    }

    /* Write type byte */
    rec_type = TLV_TYPE_CSV_FINGERPRINTS;
    if (fwrite(&rec_type, 1, 1, file) != 1) {
        return false;
    }

    /* Write 2-byte payload size */
    if (fwrite(&payload_size, 2, 1, file) != 1) {
        return false;
    }

    /* Write 2-byte entry count */
    count = (uint16_t)manager->cache_count;
    if (fwrite(&count, 2, 1, file) != 1) {
        return false;
    }

    /* Write each entry: null-terminated name + 4-byte CRC */
    for (i = 0; i < manager->cache_count; i++) {
        const char *name = manager->caches[i].csv_name;
        uint32_t   crc   = manager->caches[i].crc32;
        size_t     nlen;

        if (!name) {
            continue;
        }

        nlen = strlen(name) + 1; /* include null terminator */
        if (fwrite(name, 1, nlen, file) != nlen) {
            return false;
        }
        if (fwrite(&crc, 4, 1, file) != 1) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Read and deserialise a type 0x04 CSV fingerprint record.
 * The file position must be immediately after the type byte.
 */
bool tlv_read_csv_fingerprints(FILE *file, CSVFingerprintMap *out_map)
{
    uint16_t payload_size;
    uint16_t count;
    uint16_t i;

    if (!file || !out_map) {
        return false;
    }

    out_map->count   = 0;
    out_map->entries = NULL;

    if (fread(&payload_size, 2, 1, file) != 1) {
        return false;
    }
    if (fread(&count, 2, 1, file) != 1) {
        return false;
    }

    if (count == 0) {
        return true; /* empty but valid */
    }

    out_map->entries = (CSVFingerprint *)whd_malloc(count * sizeof(CSVFingerprint));
    if (!out_map->entries) {
        return false;
    }
    memset(out_map->entries, 0, count * sizeof(CSVFingerprint));

    for (i = 0; i < count; i++) {
        /* Read null-terminated csv_name */
        size_t pos = 0;
        int    ch;
        while ((ch = fgetc(file)) != EOF && ch != '\0' && pos < sizeof(out_map->entries[i].csv_name) - 1) {
            out_map->entries[i].csv_name[pos++] = (char)ch;
        }
        out_map->entries[i].csv_name[pos] = '\0';

        /* Read 4-byte CRC */
        if (fread(&out_map->entries[i].crc32, 4, 1, file) != 1) {
            whd_free(out_map->entries);
            out_map->entries = NULL;
            return false;
        }
    }

    out_map->count = count;
    return true;
}

/**
 * @brief Free a CSVFingerprintMap allocated by tlv_read_csv_fingerprints.
 */
void free_csv_fingerprint_map(CSVFingerprintMap *map)
{
    if (!map) {
        return;
    }
    if (map->entries) {
        whd_free(map->entries);
        map->entries = NULL;
    }
    map->count = 0;
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
        /* Write CSV fingerprint record immediately after the field map */
        if (!tlv_write_csv_fingerprints(file, &session_csv_manager)) {
            return false;
        }
        /* Write group map block (0x02) after fingerprints, before variant records */
        if (!tlv_write_group_map(file)) {
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
static bool tlv_read_metadata_map(FILE *file, FieldRegistry *field_registry) {
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
    bool success;
    TLV_PROFILE_SCOPE(profile_stamp);

    if (!csv_folder_path || !pack_types_ini_path) {
        return false;
    }

    TLV_PROFILE_START(profile_stamp);
    success = false;

    /* Cleanup any existing session */
    if (session_initialized) {
        tlv_session_finalize();
    }

    /* Initialize field registry */
    session_field_registry = field_registry_alloc();
    if (!session_field_registry) {
        goto done;
    }

    /* Build field registry from pack types configuration */
    if (!build_field_registry_from_ini(session_field_registry, pack_types_ini_path)) {
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        goto done;
    }

    /* Initialize CSV cache manager with required parameters */
    if (!csv_cache_manager_init(&session_csv_manager, NULL, csv_folder_path)) {
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        goto done;
    }

    /* Load pack types configuration */
    session_pack_types = whdtlv_load_pack_types(pack_types_ini_path, &session_pack_count);
    if (!session_pack_types) {
        csv_cache_manager_cleanup(&session_csv_manager);
        field_registry_free(session_field_registry);
        session_field_registry = NULL;
        goto done;
    }

    /* Load name-override CSV for whdtlv_prettify_title (cosmetic: affects unknown-token
     * pretty-print output only, not the TLV binary). Failure is non-fatal. */
    {
        char override_path[512];
#ifdef _WIN32
        snprintf(override_path, sizeof(override_path), "%s\\name_overrides.csv", csv_folder_path);
#else
        snprintf(override_path, sizeof(override_path), "%s/name_overrides.csv", csv_folder_path);
#endif
        whdtlv_prettify_init(override_path); /* non-fatal: returns false if file missing */
    }

    session_initialized = true;
    success = true;

done:
    TLV_PROFILE_END(TLV_PROFILE_SECTION_SESSION_INIT, profile_stamp);
    return success;
}

/**
 * @brief Process batch of filenames and generate TLV records
 */
bool tlv_session_process_batch(const char **filenames, uint32_t filename_count,
                              uint32_t pack_type_id, TLV_Record *output_records,
                              ProcessingSummary *processing_summary) {
    TLV_PROFILE_SCOPE(batch_profile_stamp);
    PackFieldMatcher *pack_matchers;
    uint32_t pack_matcher_count;
#ifdef PLATFORM_AMIGA
    BatchHeartbeat heartbeat;
    const char *last_filename;
#endif

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

#ifdef PLATFORM_AMIGA
    heartbeat.initialized = false;
    last_filename = NULL;
#endif

    /* Get pack type configuration */
    const PackType *pack_info = &session_pack_types[pack_type_id];
    pack_matchers = build_pack_field_matchers(pack_info, session_field_registry,
                                              &session_csv_manager, &pack_matcher_count);

    TLV_PROFILE_START(batch_profile_stamp);

    /* Process each filename */
    for (uint32_t i = 0; i < filename_count; i++) {
        ProcessingError error;
        TLV_PROFILE_SCOPE(record_init_profile_stamp);
        TLV_PROFILE_SCOPE(process_filename_profile_stamp);

#ifdef PLATFORM_AMIGA
        last_filename = filenames[i];
        emit_batch_heartbeat(&heartbeat,
                             filenames[i],
                             i + 1,
                             filename_count,
                             processing_summary,
                             false);
#endif

        /* Initialize output record */
        TLV_PROFILE_START(record_init_profile_stamp);
        if (!tlv_record_init(&output_records[i])) {
            TLV_PROFILE_END(TLV_PROFILE_SECTION_RECORD_INIT, record_init_profile_stamp);
            processing_summary->error_count++;
            processing_summary->total_processed++;
            continue;
        }
        TLV_PROFILE_END(TLV_PROFILE_SECTION_RECORD_INIT, record_init_profile_stamp);

        /* Process filename using our completed orchestrator */
        TLV_PROFILE_START(process_filename_profile_stamp);
        ProcessingResult result = tlv_process_filename_orchestrator(
            filenames[i], pack_info, session_field_registry,
            &session_csv_manager, pack_matchers, pack_matcher_count,
            &output_records[i], &error);
        TLV_PROFILE_END(TLV_PROFILE_SECTION_PROCESS_FILENAME, process_filename_profile_stamp);

        if (result == PROCESSING_SUCCESS) {
            processing_summary->successful_count++;
        } else {
            processing_summary->error_count++;
            /* Log error for debugging */
#if PLATFORM_AMIGA
            whdtlv_log_append("TLV processing failed for '%s': %s",
                         filenames[i], error.error_message);
#endif
        }

        processing_summary->total_processed++;
    }

#ifdef PLATFORM_AMIGA
    if (last_filename) {
        emit_batch_heartbeat(&heartbeat,
                             last_filename,
                             processing_summary->total_processed,
                             filename_count,
                             processing_summary,
                             true);
    }
#endif

    TLV_PROFILE_END(TLV_PROFILE_SECTION_BATCH_TOTAL, batch_profile_stamp);

    if (pack_matchers) {
        whd_free(pack_matchers);
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

    /* Release group map */
    group_map_free_internal(&session_group_map);

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
/* Group-ID injection and group-map write                                */

/**
 * @brief Inject group_id fields into per-file records after batch processing.
 *
 * Iterates records, derives canonical group names from each record's
 * display_name field, assigns shared uint16 group IDs, and appends a
 * group_id TLV entry (2-byte big-endian payload) to each record.
 *
 * Must be called before merging records into the aggregate and before
 * tlv_write_record_with_metadata.
 */
bool tlv_session_inject_group_ids(TLV_Record *records, uint32_t count)
{
    uint8_t  display_field_id;
    uint8_t  group_field_id;
    uint32_t i;

    if (!records || count == 0u || !session_initialized) {
        return false;
    }

    display_field_id = field_registry_get_id(session_field_registry, "display_name");
    group_field_id   = field_registry_get_id(session_field_registry, "group_id");

    if (display_field_id == 0u || group_field_id == 0u) {
        fprintf(stderr,
                "WARNING: tlv_session_inject_group_ids: "
                "display_name or group_id not found in registry\n");
        return false;
    }

    if (!group_map_init_internal(&session_group_map)) {
        return false;
    }

    for (i = 0u; i < count; i++) {
        const TLV_Entry *dn_entry;
        char             raw_name[GROUP_MAP_NAME_MAX];
        char             derived[GROUP_MAP_NAME_MAX];
        uint8_t          id_buf[2];
        uint16_t         gid;
        size_t           name_len;

        if (records[i].entry_count == 0u) {
            continue;
        }

        dn_entry = tlv_record_get_entry(&records[i], display_field_id);
        if (!dn_entry || !dn_entry->value || dn_entry->length == 0u) {
            continue;
        }

        /* Copy display_name into a NUL-terminated temp buffer. */
        name_len = (dn_entry->length < GROUP_MAP_NAME_MAX - 1u)
                       ? (size_t)dn_entry->length
                       : (size_t)(GROUP_MAP_NAME_MAX - 1u);
        memcpy(raw_name, dn_entry->value, name_len);
        raw_name[name_len] = '\0';

        /* Derive canonical group name. */
        whdtlv_derive_group_name(raw_name, derived, GROUP_MAP_NAME_MAX);

        gid = group_map_get_or_insert(&session_group_map, derived);
        if (gid == 0u) {
            /* OOM or overflow — leave this record without group_id. */
            continue;
        }

        /* Emit as 2-byte big-endian payload. */
        id_buf[0] = (uint8_t)((gid >> 8) & 0xFFu);
        id_buf[1] = (uint8_t)(gid        & 0xFFu);

        tlv_record_add_entry(&records[i], group_field_id, id_buf, 2u);
    }

    return true;
}

/**
 * @brief Write the group-map header block (type 0x02) to a TLV file.
 *
 * Wire format:
 *   [1 byte  : 0x02 (TLV_TYPE_GROUP_MAP)]
 *   [2 bytes LE: payload_size]
 *   [2 bytes LE: group_count]
 *   per entry:
 *     [2 bytes BE: group_id]
 *     [1 byte   : name_len]
 *     [name_len bytes: group_name (no NUL terminator)]
 */
static bool tlv_write_group_map(FILE *file)
{
    uint8_t  block_type;
    uint16_t payload_size;
    uint16_t group_count;
    uint32_t i;

    if (!file) {
        return false;
    }

    if (session_group_map.count == 0u) {
        return true; /* no groups — nothing to write */
    }

    /* Calculate payload: 2 bytes (group_count field) + per-entry bytes. */
    payload_size = 2u;
    for (i = 0u; i < session_group_map.count; i++) {
        size_t nlen = strlen(session_group_map.entries[i].name);
        if (nlen > 255u) {
            nlen = 255u;
        }
        payload_size += (uint16_t)(2u + 1u + nlen); /* id(2) + len(1) + name */
    }

    block_type  = TLV_TYPE_GROUP_MAP;
    group_count = (uint16_t)session_group_map.count;

    if (fwrite(&block_type,   1u, 1u, file) != 1u) {
        return false;
    }
    if (fwrite(&payload_size, 2u, 1u, file) != 1u) {
        return false;
    }
    if (fwrite(&group_count,  2u, 1u, file) != 1u) {
        return false;
    }

    for (i = 0u; i < session_group_map.count; i++) {
        uint8_t  id_be[2];
        uint8_t  name_len_byte;
        size_t   nlen;
        uint16_t gid = session_group_map.entries[i].id;

        nlen = strlen(session_group_map.entries[i].name);
        if (nlen > 255u) {
            nlen = 255u;
        }

        id_be[0]      = (uint8_t)((gid >> 8) & 0xFFu);
        id_be[1]      = (uint8_t)(gid        & 0xFFu);
        name_len_byte = (uint8_t)nlen;

        if (fwrite(id_be,          1u, 2u, file) != 2u) {
            return false;
        }
        if (fwrite(&name_len_byte, 1u, 1u, file) != 1u) {
            return false;
        }
        if (nlen > 0u) {
            if (fwrite(session_group_map.entries[i].name, 1u, nlen, file) != nlen) {
                return false;
            }
        }
    }

    return true;
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
    PackFieldMatcher *pack_matchers = NULL;
    uint32_t pack_matcher_count = 0;
    if (session_pack_count > 0) {
        pack_matchers = build_pack_field_matchers(&session_pack_types[0], session_field_registry,
                                                  &session_csv_manager, &pack_matcher_count);
        ProcessingResult result = tlv_process_filename_orchestrator(
            pack_name, &session_pack_types[0], session_field_registry,
            &session_csv_manager, pack_matchers, pack_matcher_count, record, &error);

        if (pack_matchers) {
            whd_free(pack_matchers);
        }

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
