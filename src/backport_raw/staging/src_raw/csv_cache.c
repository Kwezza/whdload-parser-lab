/* csv_cache.c - Global CSV Cache Manager Implementation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * High-performance CSV cache system optimized for 4MB Amiga systems with
 * adaptive memory management and graceful degradation when CSV files are missing.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#include <platform.h>
#include <tlv_filename/csv_cache.h>
#include <tlv_filename/error_handling.h>
#include <io/writeLog.h>

#include <string.h>
#include <stdio.h>
#include <utils/prettify.h>
#include <platform/platform_string.h>

/* Debug logging control for this module */
#ifndef CSV_DEBUG
#define CSV_DEBUG 0
#endif
#if CSV_DEBUG
#define CSVDBG printf
#else
#define CSVDBG(...) do { } while (0)
#endif

/*------------------------------------------------------------------------*/
/* Constants */

#define CSV_DEFAULT_MEMORY_LIMIT_KB     512
#define CSV_HASH_TABLE_MIN_SIZE         64
#define CSV_MAX_LINE_LENGTH             512
#define CSV_MAX_TOKEN_LENGTH            128
#define CSV_UNKNOWN_TOKENS_INITIAL      32

/*------------------------------------------------------------------------*/
/* Internal Hash Table Functions */

/**
 * @brief Simple djb2 hash function for token lookup
 */
static uint32_t csv_hash_token(const char *token, uint32_t capacity) {
    uint32_t hash = 5381;
    const unsigned char *str = (const unsigned char *)token;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash % capacity;
}

/**
 * @brief Calculate optimal hash table capacity for entry count
 */
static uint32_t csv_calculate_capacity(uint32_t entry_count) {
    uint32_t capacity = CSV_HASH_TABLE_MIN_SIZE;
    /* Load factor 0.75 = 3/4, so capacity should be entry_count * 4 / 3 */
    uint32_t target = (entry_count * 4U) / 3U;

    while (capacity < target) {
        capacity *= 2;
    }

    return capacity;
}

/*------------------------------------------------------------------------*/
/* Helpers for special.csv filtering */

/* Case-insensitive substring search (ASCII only) */
static bool str_icontains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) {
        return false;
    }
    for (const char *h = haystack; *h; ++h) {
        const char *h_it = h;
        const char *n_it = needle;
        while (*h_it && *n_it) {
            char hc = *h_it;
            char nc = *n_it;
            if (hc >= 'A' && hc <= 'Z') { hc = (char)(hc + 32); }
            if (nc >= 'A' && nc <= 'Z') { nc = (char)(nc + 32); }
            if (hc != nc) { break; }
            ++h_it; ++n_it;
        }
        if (*n_it == '\0') {
            return true; /* needle fully matched */
        }
    }
    return false;
}

/* Fallback: scan CSV directory for token when caches are incomplete */
static bool csv_token_exists_in_any_csv(GlobalCSVManager *mgr, const char *token) {
    if (!mgr || !token) {
        return false;
    }
    /* Short-circuit via loaded caches */
    if (csv_cache_find_token_source(mgr, token) != NULL) {
        return true;
    }
    /* Validate base path */
    if (mgr->csv_base_path[0] == '\0') {
        return false;
    }

    whd_dir_t *dir = whd_opendir(mgr->csv_base_path);
    if (!dir) {
        return false;
    }

    whd_dir_entry_t ent;
    bool found = false;
    while (!found && whd_readdir(dir, &ent) == 1) {
        size_t nlen = strlen(ent.name);
        if (ent.is_directory || nlen < 5) {
            continue;
        }
        /* Only .csv files */
        if (!(nlen >= 4 && (strcmp(ent.name + (nlen - 4), ".csv") == 0))) {
            continue;
        }
        /* Ignore any special*.csv (including copies or variants) */
        if (str_icontains(ent.name, "special")) {
            continue;
        }

        char csv_path[1024];
#if PLATFORM_HOST && defined(_WIN32)
        snprintf(csv_path, sizeof(csv_path), "%s\\%s", mgr->csv_base_path, ent.name);
#else
        snprintf(csv_path, sizeof(csv_path), "%s/%s", mgr->csv_base_path, ent.name);
#endif
        if (csv_direct_file_lookup(csv_path, token) != 0) {
            found = true;
            break;
        }
    }
    whd_closedir(dir);
    return found;
}

/**
 * @brief Insert token-ID pair into CSV cache hash table
 */
static bool csv_cache_insert(CSVCache *cache, const char *token, const char *long_name, uint32_t id) {
    if (!cache || !token || cache->entry_count >= cache->capacity) {
        return false;
    }

    uint32_t index = csv_hash_token(token, cache->capacity);
    uint32_t original_index = index;

    /* Linear probing for collision resolution */
    while (cache->entries[index].token != NULL) {
        /* Check for duplicate token */
        if (strcmp(cache->entries[index].token, token) == 0) {
            return false; /* Already exists */
        }

        index = (index + 1) % cache->capacity;
        if (index == original_index) {
            return false; /* Table full */
        }
    }

    /* Allocate and copy token string */
    cache->entries[index].token = whd_malloc(strlen(token) + 1);
    if (!cache->entries[index].token) {
        return false;
    }

    strcpy(cache->entries[index].token, token);
    if (long_name && long_name[0] != '\0') {
        cache->entries[index].long_name = whd_malloc(strlen(long_name) + 1);
        if (!cache->entries[index].long_name) {
            /* still store token/id even if long alloc fails */
        } else {
            strcpy(cache->entries[index].long_name, long_name);
        }
    } else {
        cache->entries[index].long_name = NULL;
    }
    cache->entries[index].id = id;
    cache->entry_count++;

    return true;
}

/**
 * @brief Lookup token in CSV cache hash table
 */
static uint32_t csv_cache_find(CSVCache *cache, const char *token) {
    if (!cache || !token || cache->entry_count == 0) {
        return 0;
    }

    uint32_t index = csv_hash_token(token, cache->capacity);
    uint32_t original_index = index;

    while (cache->entries[index].token != NULL) {
        if (strcmp(cache->entries[index].token, token) == 0) {
            return cache->entries[index].id;
        }

        index = (index + 1) % cache->capacity;
        if (index == original_index) {
            break; /* Searched entire table */
        }
    }

    return 0; /* Not found */
}

/**
 * @brief Case-insensitive lookup token in CSV cache hash table (linear scan fallback)
 */
static uint32_t csv_cache_find_ci(CSVCache *cache, const char *token) {
    if (!cache || !token || cache->entry_count == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < cache->capacity; i++) {
        if (cache->entries[i].token) {
            if (whd_strcasecmp(cache->entries[i].token, token) == 0) {
                return cache->entries[i].id;
            }
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* CSV File Parsing */

/**
 * @brief Parse CSV line and extract ID and name
 * @param line CSV line to parse
 * @param id Output: extracted ID
 * @param name Output: extracted name (allocated)
 * @return true if successful, false on error
 */
static bool csv_parse_line(const char *line, uint32_t *id, char **name, char **long_name, bool *is_default_marker) {
    if (!line || !id || !name) {
        return false;
    }

    /* Normalize start pointer: skip UTF-8 BOM and leading whitespace */
    const unsigned char *ul = (const unsigned char *)line;
    const char *s = line;
    if (ul[0] == 0xEF && ul[1] == 0xBB && ul[2] == 0xBF) {
        s = (const char *)(ul + 3);
    }
    while (*s == ' ' || *s == '\t' || *s == '\r') {
        s++;
    }

    /* Skip empty lines and comments */
    if (s[0] == '\0' || s[0] == '#' || s[0] == '\n') {
        return false;
    }

    /* Find first comma */
    const char *comma = strchr(s, ',');
    if (!comma) {
        return false;
    }

    /* Parse ID */
    char id_str[16];
    size_t id_len = (size_t)(comma - s);
    if (id_len >= sizeof(id_str)) {
        return false;
    }

    strncpy(id_str, s, id_len);
    id_str[id_len] = '\0';
    *id = (uint32_t)atoi(id_str);

    if (*id == 0) {
        return false; /* Invalid ID */
    }

    /* Extract name (skip comma) */
    const char *name_start = comma + 1;
    const char *name_end = strchr(name_start, ',');
    if (!name_end) {
        name_end = strchr(name_start, '\n');
        if (!name_end) {
            name_end = name_start + strlen(name_start);
        }
    }

    size_t name_len = name_end - name_start;
    if (name_len == 0 || name_len >= CSV_MAX_TOKEN_LENGTH) {
        return false;
    }

    *name = whd_malloc(name_len + 1);
    if (!*name) {
        return false;
    }

    strncpy(*name, name_start, name_len);
    (*name)[name_len] = '\0';

    /* Optional third column: long/wordy name up to next comma or end */
    if (long_name) {
        *long_name = NULL;
        if (*name_end == ',') {
            const char *long_start = name_end + 1;
            const char *long_end = strchr(long_start, ',');
            if (!long_end) { long_end = strchr(long_start, '\n'); if (!long_end) { long_end = long_start + strlen(long_start); } }
            size_t l_len = (size_t)(long_end - long_start);
            if (l_len > 0 && l_len < CSV_MAX_TOKEN_LENGTH) {
                *long_name = whd_malloc(l_len + 1);
                if (*long_name) { strncpy(*long_name, long_start, l_len); (*long_name)[l_len] = '\0'; }
            }
            /* Optional 4th column: default marker */
            if (is_default_marker) {
                *is_default_marker = false;
                if (*long_end == ',') {
                    const char *m_start = long_end + 1; const char *m_end = strchr(m_start, '\n'); if (!m_end) { m_end = m_start + strlen(m_start); }
                    size_t m_len = (size_t)(m_end - m_start);
                    if (m_len > 0 && m_len < 16) {
                        char mbuf[16]; memcpy(mbuf, m_start, m_len); mbuf[m_len]='\0';
                        for (size_t i=0;i<m_len;i++){ char c=mbuf[i]; if (c>='A'&&c<='Z'){ mbuf[i]=(char)(c+32);} }
                        if (strcmp(mbuf, "default") == 0) { *is_default_marker = true; }
                    }
                }
            }
        } else if (is_default_marker) { *is_default_marker = false; }
    } else if (is_default_marker) { *is_default_marker = false; }

    return true;
}

/**
 * @brief Load CSV file and populate cache
 */
static bool csv_load_file_into_cache(CSVCache *cache, const char *csv_path) {
    if (!cache || !csv_path) {
        return false;
    }

#if PLATFORM_HOST
    CSVDBG("DEBUG: csv_load_file_into_cache opening file: '%s'\n", csv_path);

    /* Test manual fopen for debugging */
    FILE *test_file = fopen(csv_path, "r");
    if (test_file) {
    CSVDBG("DEBUG: Manual fopen SUCCESS\n");
        fclose(test_file);
    } else {
    CSVDBG("DEBUG: Manual fopen FAILED\n");
    CSVDBG("DEBUG: fopen error\n");
    }
#endif

    FILE *file = fopen(csv_path, "r");  /* Use fopen directly for testing */
    if (!file) {
#if PLATFORM_HOST
    CSVDBG("DEBUG: whd_fopen failed for file: '%s'\n", csv_path);
#endif
        return false;
    }

#if PLATFORM_HOST
    CSVDBG("DEBUG: File opened successfully, starting to parse...\n");
#endif

    char line[CSV_MAX_LINE_LENGTH];
    uint32_t entry_count = 0;

    /* First pass: count entries */
    while (fgets(line, sizeof(line), file)) {
        uint32_t id; char *name; char *long_name = NULL; bool is_def=false;
        if (csv_parse_line(line, &id, &name, &long_name, &is_def)) {
            entry_count++;
#if PLATFORM_HOST
            CSVDBG("DEBUG: Parsed entry %u: id=%u, name=%s\n", entry_count, id, name ? name : "NULL");
#endif
            whd_free(name);
            if (long_name) { whd_free(long_name); }
        }
    }

#if PLATFORM_HOST
    CSVDBG("DEBUG: First pass complete. Entry count: %u\n", entry_count);
#endif

    if (entry_count == 0) {
#if PLATFORM_HOST
    CSVDBG("DEBUG: No entries found, returning false\n");
#endif
        whd_fclose(file);
        return false;
    }

    /* Calculate capacity and allocate hash table */
    cache->capacity = csv_calculate_capacity(entry_count);
    cache->entries = whd_malloc(cache->capacity * sizeof(CSVEntry));
    if (!cache->entries) {
        whd_fclose(file);
        return false;
    }

    /* Initialize hash table */
    memset(cache->entries, 0, cache->capacity * sizeof(CSVEntry));
    cache->entry_count = 0;

    /* Second pass: populate hash table */
    rewind(file);
    cache->has_default_token = false; cache->default_token_id = 0;
    while (fgets(line, sizeof(line), file)) {
        uint32_t id; char *name; char *long_name = NULL; bool is_def=false;
        if (csv_parse_line(line, &id, &name, &long_name, &is_def)) {
            if (!csv_cache_insert(cache, name, long_name, id)) {
#if PLATFORM_AMIGA
                append_to_log("WARNING: Failed to insert token '%s' into cache", name);
#endif
            }
            if (is_def && !cache->has_default_token) { cache->has_default_token = true; cache->default_token_id = id; }
            whd_free(name);
            if (long_name) { whd_free(long_name); }
        }
    }

    whd_fclose(file);

    /* Calculate memory usage in KB using explicit integer division */
    cache->memory_usage_kb = (uint32_t)(((cache->capacity * sizeof(CSVEntry)) +
                             (cache->entry_count * 32U)) / 1024U); /* Approximate */

    return true;
}

/*------------------------------------------------------------------------*/
/* Manager Lifecycle Functions */

/**
 * @brief Initialize CSV cache manager with default settings
 */
bool csv_cache_manager_init(GlobalCSVManager *manager, struct AppCtx *ctx, const char *csv_base_path) {
    return csv_cache_manager_init_with_config(manager, ctx, csv_base_path, CSV_DEFAULT_MEMORY_LIMIT_KB);
}

/**
 * @brief Initialize CSV cache manager with specific memory configuration
 */
bool csv_cache_manager_init_with_config(GlobalCSVManager *manager,
                                       struct AppCtx *ctx, /* currently unused */
                                       const char *csv_base_path,
                                       uint32_t memory_limit_kb) {
    if (!manager || !csv_base_path) {
        return false;
    }

    /* ctx is currently unused in this implementation */
#if PLATFORM_HOST
    (void)ctx;
#endif

    /* Initialize structure */
    memset(manager, 0, sizeof(GlobalCSVManager));
    manager->memory_limit_kb = memory_limit_kb;
    manager->cache_enabled = true;

    /* Store CSV base path */
    strncpy(manager->csv_base_path, csv_base_path, sizeof(manager->csv_base_path) - 1);
    manager->csv_base_path[sizeof(manager->csv_base_path) - 1] = '\0';

    /* Allocate initial arrays */
    manager->caches = whd_malloc(32 * sizeof(CSVCache)); /* Start with 32 slots */
    if (!manager->caches) {
        return false;
    }

    manager->unknown_tokens = whd_malloc(CSV_UNKNOWN_TOKENS_INITIAL * sizeof(char *));
    manager->unknown_filenames = whd_malloc(CSV_UNKNOWN_TOKENS_INITIAL * sizeof(char *));
    manager->unknown_pack_ids = whd_malloc(CSV_UNKNOWN_TOKENS_INITIAL * sizeof(uint8_t));
    manager->missing_csv_names = whd_malloc(32 * sizeof(char *));
    manager->unknown_capacity = CSV_UNKNOWN_TOKENS_INITIAL;

    if (!manager->unknown_tokens || !manager->unknown_filenames || !manager->unknown_pack_ids || !manager->missing_csv_names) {
        csv_cache_manager_cleanup(manager);
        return false;
    }

#if PLATFORM_AMIGA
    append_to_log("CSV cache manager initialized with %luKB memory limit", (unsigned long)memory_limit_kb);
#endif

    return true;
}

/**
 * @brief Clean up CSV cache manager and all resources
 */
void csv_cache_manager_cleanup(GlobalCSVManager *manager) {
    if (!manager) {
        return;
    }

    /* Clean up CSV caches */
    if (manager->caches) {
        for (uint32_t i = 0; i < manager->cache_count; i++) {
            CSVCache *cache = &manager->caches[i];
            if (cache->entries) {
                for (uint32_t j = 0; j < cache->capacity; j++) {
                    if (cache->entries[j].token) {
                        whd_free(cache->entries[j].token);
                        if (cache->entries[j].long_name) { whd_free(cache->entries[j].long_name); }
                    }
                }
                whd_free(cache->entries);
            }
            if (cache->csv_name) {
                whd_free(cache->csv_name);
            }
        }
        whd_free(manager->caches);
    }

    /* Clean up special cache */
    if (manager->special_cache) {
        if (manager->special_cache->entries) {
            for (uint32_t j = 0; j < manager->special_cache->capacity; j++) {
                if (manager->special_cache->entries[j].token) {
                    whd_free(manager->special_cache->entries[j].token);
                    if (manager->special_cache->entries[j].long_name) { whd_free(manager->special_cache->entries[j].long_name); }
                }
            }
            whd_free(manager->special_cache->entries);
        }
        if (manager->special_cache->csv_name) {
            whd_free(manager->special_cache->csv_name);
        }
        whd_free(manager->special_cache);
    }

    /* Clean up unknown tokens */
    if (manager->unknown_tokens) {
        for (uint32_t i = 0; i < manager->unknown_count; i++) {
            if (manager->unknown_tokens[i]) {
                whd_free(manager->unknown_tokens[i]);
            }
            if (manager->unknown_filenames[i]) {
                whd_free(manager->unknown_filenames[i]);
            }
        }
        whd_free(manager->unknown_tokens);
        whd_free(manager->unknown_filenames);
        if (manager->unknown_pack_ids) { whd_free(manager->unknown_pack_ids); }
    }

    /* Clean up missing CSV names */
    if (manager->missing_csv_names) {
        for (uint32_t i = 0; i < manager->missing_csv_count; i++) {
            if (manager->missing_csv_names[i]) {
                whd_free(manager->missing_csv_names[i]);
            }
        }
        whd_free(manager->missing_csv_names);
    }

    memset(manager, 0, sizeof(GlobalCSVManager));
}

/*------------------------------------------------------------------------*/
/* CSV File Operations */

/**
 * @brief Load specific CSV file into cache with graceful degradation
 */
bool csv_cache_load_file(GlobalCSVManager *manager, const char *csv_name) {
    if (!manager || !csv_name) {
        return false;
    }

    /* Build CSV file path using stored base path */
    char csv_path[512];
#if PLATFORM_HOST && defined(_WIN32)
    /* Use backslash on Windows */
    snprintf(csv_path, sizeof(csv_path), "%s\\%s.csv", manager->csv_base_path, csv_name);
#else
    /* Use forward slash on other platforms */
    snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", manager->csv_base_path, csv_name);
#endif

    /* Debug: Show the actual path being checked */
#if PLATFORM_HOST
    CSVDBG("DEBUG: Attempting to load CSV file: '%s'\n", csv_path);
    CSVDBG("DEBUG: Base path: '%s', CSV name: '%s'\n", manager->csv_base_path, csv_name);
#endif

    /* Check if file exists */
    if (whd_access(csv_path, 0) != 0) {
        /* Add to missing files list */
        if (manager->missing_csv_count < 32) {
            manager->missing_csv_names[manager->missing_csv_count] = whd_malloc(strlen(csv_name) + 1);
            if (manager->missing_csv_names[manager->missing_csv_count]) {
                strcpy(manager->missing_csv_names[manager->missing_csv_count], csv_name);
                manager->missing_csv_count++;
            }
        }

#if PLATFORM_AMIGA
        append_to_log("WARNING: Missing CSV file '%s' - field will be skipped", csv_name);
#endif
        return false; /* Not loaded, but not fatal */
    }

#if PLATFORM_HOST
    CSVDBG("DEBUG: File exists, proceeding to load...\n");
#endif

    /* Check memory limit before loading */
    if (manager->cache_enabled && manager->total_memory_kb >= manager->memory_limit_kb) {
        manager->cache_enabled = false;
#if PLATFORM_AMIGA
    append_to_log("CSV caching disabled: memory limit %luKB exceeded", (unsigned long)manager->memory_limit_kb);
#endif
        return false;
    }

    /* Create new cache entry */
    CSVCache *cache = &manager->caches[manager->cache_count];
    memset(cache, 0, sizeof(CSVCache));

    cache->csv_name = whd_malloc(strlen(csv_name) + 1);
    if (!cache->csv_name) {
        return false;
    }
    strcpy(cache->csv_name, csv_name);

    /* Load CSV file into cache */
#if PLATFORM_HOST
    CSVDBG("DEBUG: About to call csv_load_file_into_cache with path: '%s'\n", csv_path);
#endif
    if (!csv_load_file_into_cache(cache, csv_path)) {
#if PLATFORM_HOST
    CSVDBG("DEBUG: csv_load_file_into_cache failed!\n");
#endif
        whd_free(cache->csv_name);
        return false;
    }

    manager->cache_count++;
    manager->total_memory_kb += cache->memory_usage_kb;

#if PLATFORM_AMIGA
    append_to_log("Loaded CSV '%s': %lu entries, %luKB memory",
                 csv_name, (unsigned long)cache->entry_count, (unsigned long)cache->memory_usage_kb);
#endif

    return true;
}

/**
 * @brief Load existing special.csv into cache for duplicate checking
 */
bool csv_cache_load_special_csv(GlobalCSVManager *manager, const char *special_csv_path) {
    if (!manager || !special_csv_path) {
        return false;
    }

    if (whd_access(special_csv_path, 0) != 0) {
        return false; /* File doesn't exist */
    }

    /* Allocate special cache */
    manager->special_cache = whd_malloc(sizeof(CSVCache));
    if (!manager->special_cache) {
        return false;
    }

    memset(manager->special_cache, 0, sizeof(CSVCache));
    manager->special_cache->csv_name = whd_malloc(strlen("special") + 1);
    if (!manager->special_cache->csv_name) {
        whd_free(manager->special_cache);
        manager->special_cache = NULL;
        return false;
    }
    strcpy(manager->special_cache->csv_name, "special");

    /* Load special.csv */
    if (!csv_load_file_into_cache(manager->special_cache, special_csv_path)) {
        whd_free(manager->special_cache->csv_name);
        whd_free(manager->special_cache);
        manager->special_cache = NULL;
        return false;
    }

    return true;
}

/*------------------------------------------------------------------------*/
/* Lookup Operations */

/**
 * @brief Fast lookup: find ID for token in specific CSV cache
 */
uint32_t csv_cache_lookup(GlobalCSVManager *manager, const char *csv_name, const char *token) {
    if (!manager || !csv_name || !token) {
        return 0;
    }

    /* If caching disabled, use direct file access */
    if (!manager->cache_enabled) {
        char csv_path[512];
        snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", manager->csv_base_path, csv_name);
        return csv_direct_file_lookup(csv_path, token);
    }

    /* Try to find an already-loaded cache with this name */
    CSVCache *target = NULL;
    for (uint32_t i = 0; i < manager->cache_count; i++) {
        if (strcmp(manager->caches[i].csv_name, csv_name) == 0) {
            target = &manager->caches[i];
            break;
        }
    }

    /* If not loaded yet, lazily load this CSV now */
    if (!target) {
        if (!csv_cache_load_file(manager, csv_name)) {
            return 0; /* Missing or failed to load */
        }
        /* Newly loaded cache will be at the end */
        if (manager->cache_count > 0) {
            target = &manager->caches[manager->cache_count - 1];
        }
    }

    if (target) {
        uint32_t id = csv_cache_find(target, token);
        if (id == 0) {
            /* Fallback: case-insensitive match for minor case differences */
            id = csv_cache_find_ci(target, token);
        }
#if PLATFORM_HOST
    CSVDBG("DEBUG: csv_cache_lookup('%s', '%s') -> %u\n", csv_name, token, id);
#endif
        return id;
    }

    return 0;
}

/**
 * @brief Find which CSV contains the given token across all caches
 */
const char *csv_cache_find_token_source(GlobalCSVManager *manager, const char *token) {
    if (!manager || !token) {
        return NULL;
    }

    for (uint32_t i = 0; i < manager->cache_count; i++) {
        if (csv_cache_find(&manager->caches[i], token) != 0) {
            return manager->caches[i].csv_name;
        }
    }

    return NULL; /* Not found in any cache */
}

/**
 * @brief Direct file lookup (used when caching disabled)
 */
uint32_t csv_direct_file_lookup(const char *csv_path, const char *token) {
    if (!csv_path || !token) {
        return 0;
    }

    FILE *file = whd_fopen(csv_path, "r");
    if (!file) {
        return 0;
    }

    char line[CSV_MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        uint32_t id;
        char *name; char *long_name = NULL;
    if (csv_parse_line(line, &id, &name, &long_name, NULL)) {
            bool match = (strcmp(name, token) == 0);
            whd_free(name);
            if (long_name) { whd_free(long_name); }
            if (match) {
                whd_fclose(file);
                return id;
            }
        }
    }

    whd_fclose(file);
    return 0;
}

/**
 * @brief Reverse lookup: find token for ID within a given CSV cache
 */
const char *csv_cache_reverse_lookup(GlobalCSVManager *manager, const char *csv_name, uint32_t id, bool want_long) {
    if (!manager || !csv_name || id == 0) {
        return NULL;
    }

    /* If caching is disabled, attempt a slow reverse read from file */
    if (!manager->cache_enabled) {
        char csv_path[512];
#if PLATFORM_HOST && defined(_WIN32)
        snprintf(csv_path, sizeof(csv_path), "%s\\%s.csv", manager->csv_base_path, csv_name);
#else
        snprintf(csv_path, sizeof(csv_path), "%s/%s.csv", manager->csv_base_path, csv_name);
#endif
        FILE *file = whd_fopen(csv_path, "r");
        if (!file) {
            return NULL;
        }
        char line[CSV_MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), file)) {
            uint32_t lid; char *name; char *long_name = NULL;
            if (csv_parse_line(line, &lid, &name, &long_name, NULL)) {
                if (lid == id) {
                    /* Return a static buffer to keep API simple in direct mode */
                    static char token_buf[CSV_MAX_TOKEN_LENGTH];
                    const char *src = (want_long && long_name && long_name[0]) ? long_name : name;
                    strncpy(token_buf, src, sizeof(token_buf) - 1);
                    token_buf[sizeof(token_buf) - 1] = '\0';
                    if (name) whd_free(name);
                    if (long_name) whd_free(long_name);
                    whd_fclose(file);
                    return token_buf;
                }
                if (name) whd_free(name);
                if (long_name) whd_free(long_name);
            }
        }
        whd_fclose(file);
        return NULL;
    }

    /* Search loaded caches for matching csv_name and then scan entries */
    for (uint32_t i = 0; i < manager->cache_count; i++) {
        CSVCache *cache = &manager->caches[i];
        if (cache->csv_name && strcmp(cache->csv_name, csv_name) == 0) {
            for (uint32_t j = 0; j < cache->capacity; j++) {
                if (cache->entries[j].token && cache->entries[j].id == id) {
                    if (want_long && cache->entries[j].long_name && cache->entries[j].long_name[0]) {
                        return cache->entries[j].long_name;
                    }
                    return cache->entries[j].token; /* owned by cache */
                }
            }
            break; /* matched csv_name; no need to check others */
        }
    }
    return NULL;
}

uint32_t csv_cache_get_default_token(GlobalCSVManager *manager, const char *csv_name, bool *has_default) {
    if (has_default) { *has_default = false; }
    if (!manager || !csv_name) { return 0; }
    for (uint32_t i=0;i<manager->cache_count;i++) {
        CSVCache *c = &manager->caches[i];
        if (c->csv_name && strcmp(c->csv_name, csv_name)==0) {
            if (has_default) { *has_default = c->has_default_token; }
            return c->has_default_token ? c->default_token_id : 0;
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Unknown Token Management */

/**
 * @brief Add unknown token to tracking list with source filename
 */
bool csv_cache_add_unknown_token_ex(GlobalCSVManager *manager, const char *token, const char *filename, uint8_t pack_id) {
    if (!manager || !token || !filename) {
        return false;
    }

    /* Check for duplicates in current unknown list */
    for (uint32_t i = 0; i < manager->unknown_count; i++) {
        if (strcmp(manager->unknown_tokens[i], token) == 0) {
            return false; /* Already in list */
        }
    }

    /* Grow arrays if needed, with hard cap to avoid unbounded memory */
    if (manager->unknown_count >= manager->unknown_capacity) {
        const uint32_t HARD_CAP = 65536; /* Max unknowns tracked */
        uint32_t new_capacity = manager->unknown_capacity * 2;
        if (new_capacity > HARD_CAP) {
            new_capacity = HARD_CAP;
        }
        if (new_capacity > manager->unknown_capacity) {
            char **new_tokens = whd_malloc(new_capacity * sizeof(char*));
            char **new_files = whd_malloc(new_capacity * sizeof(char*));
            uint8_t *new_packs = whd_malloc(new_capacity * sizeof(uint8_t));
            if (!new_tokens || !new_files || !new_packs) {
                if (new_tokens) whd_free(new_tokens);
                if (new_files) whd_free(new_files);
                if (new_packs) whd_free(new_packs);
                return false;
            }
            /* Copy existing */
            memcpy(new_tokens, manager->unknown_tokens, manager->unknown_count * sizeof(char*));
            memcpy(new_files, manager->unknown_filenames, manager->unknown_count * sizeof(char*));
            memcpy(new_packs, manager->unknown_pack_ids, manager->unknown_count * sizeof(uint8_t));
            /* Free old and assign */
            whd_free(manager->unknown_tokens);
            whd_free(manager->unknown_filenames);
            whd_free(manager->unknown_pack_ids);
            manager->unknown_tokens = new_tokens;
            manager->unknown_filenames = new_files;
            manager->unknown_pack_ids = new_packs;
            manager->unknown_capacity = new_capacity;
        } else {
            /* At hard cap: stop tracking further unknowns */
            return false;
        }
    }

    /* Allocate token string */
    manager->unknown_tokens[manager->unknown_count] = whd_malloc(strlen(token) + 1);
    if (!manager->unknown_tokens[manager->unknown_count]) {
        return false;
    }
    strcpy(manager->unknown_tokens[manager->unknown_count], token);

    /* Allocate filename string */
    manager->unknown_filenames[manager->unknown_count] = whd_malloc(strlen(filename) + 1);
    if (!manager->unknown_filenames[manager->unknown_count]) {
        whd_free(manager->unknown_tokens[manager->unknown_count]);
        return false;
    }
    strcpy(manager->unknown_filenames[manager->unknown_count], filename);

    /* Store pack id */
    manager->unknown_pack_ids[manager->unknown_count] = pack_id;

    manager->unknown_count++;

#if PLATFORM_AMIGA
    append_to_log("UNKNOWN: Token '%s' from '%s' added to special.csv review list", token, filename);
#endif

    return true;
}

bool csv_cache_add_unknown_token(GlobalCSVManager *manager, const char *token, const char *filename) {
    return csv_cache_add_unknown_token_ex(manager, token, filename, 0);
}

/**
 * @brief Check if token already exists in special.csv
 */
bool csv_cache_is_token_in_special(GlobalCSVManager *manager, const char *token) {
    if (!manager || !token || !manager->special_cache) {
        return false;
    }

    return (csv_cache_find(manager->special_cache, token) != 0);
}

/**
 * @brief Write all unknown tokens to special.csv with duplicate filtering
 */
/* Helper: trim leading/trailing spaces (in place) */
static void trim_spaces(char *s) {
    if (!s) { return; }
    char *start = s;
    while (*start == ' ' || *start == '\t') { start++; }
    if (start != s) { memmove(s, start, strlen(start) + 1); }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
}

/* Helper: write one or multiple lines for a possibly compound token separated by '&' */
static void write_split_special_lines(FILE *out,
                                     uint32_t id,
                                     const char *name,
                                     const char *desc,
                                     const char *filename,
                                     unsigned pack_id) {
    if (!out || !name) { return; }

    /* Make a mutable copy */
    char buf[CSV_MAX_LINE_LENGTH];
    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *part = whd_strtok_r(buf, "&", &saveptr);
    while (part) {
        char token_part[CSV_MAX_TOKEN_LENGTH];
        strncpy(token_part, part, sizeof(token_part) - 1);
        token_part[sizeof(token_part) - 1] = '\0';
        trim_spaces(token_part);

        /* Pretty for this part */
        char pretty_buf[256];
        const char *pretty_out = token_part;
        if (prettify_title(token_part, pretty_buf, sizeof(pretty_buf))) {
            pretty_out = pretty_buf;
        }

    {
        const char *desc_out = desc ? desc : "";
        const char *file_out = filename ? filename : "";
        const char *pretty_print = pretty_out ? pretty_out : "";
        fprintf(out, "%lu,%s,%s,%s,%lu,%s\n",
            (unsigned long)id,
            token_part,
            desc_out,
            file_out,
            (unsigned long)pack_id,
            pretty_print);
    }

    part = whd_strtok_r(NULL, "&", &saveptr);
    }
}

bool csv_cache_update_special_csv(GlobalCSVManager *manager, const char *special_csv_path) {
    if (!manager || !special_csv_path) {
        return true; /* Nothing to do */
    }

    /* Determine if existing file is present and get highest ID while preparing to rewrite */
    uint32_t highest_id = 0;
    bool has_existing = (whd_access(special_csv_path, 0) == 0);

    /* Prepare temp path for atomic rewrite */
    char tmp_path[576];
#if PLATFORM_HOST && defined(_WIN32)
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", special_csv_path);
#else
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", special_csv_path);
#endif

    FILE *out = whd_fopen(tmp_path, "w");
    if (!out) {
        return false;
    }

    bool wrote_any = false;      /* any data rows emitted? */
    bool header_written = false; /* write header only if we emit at least one row */

    /* If existing file, read and rewrite with splitting applied */
    if (has_existing) {
        FILE *in = whd_fopen(special_csv_path, "r");
        if (in) {
            char line[CSV_MAX_LINE_LENGTH];
            bool first = true;
            uint32_t removed_recognized = 0; /* Count of entries dropped because now recognized */
            while (fgets(line, sizeof(line), in)) {
                /* Skip header line from old file */
                if (first) { first = false; }
                if (strncmp(line, "ID,Name,Description,Filename,PackID,Pretty", 44) == 0) {
                    /* Skip any header occurrences, not just the first */
                    continue;
                }

                /* Parse ID */
                char *p = line;
                char *c1 = strchr(p, ',');
                if (!c1) { continue; }
                char idbuf[16];
                size_t idlen = (size_t)(c1 - p);
                if (idlen >= sizeof(idbuf)) { continue; }
                memcpy(idbuf, p, idlen); idbuf[idlen] = '\0';
                /* Guard against non-numeric IDs (e.g., stray header-like lines) */
                bool id_numeric = true;
                for (size_t ii = 0; ii < idlen; ii++) {
                    if (idbuf[ii] < '0' || idbuf[ii] > '9') { id_numeric = false; break; }
                }
                if (!id_numeric) {
                    /* Skip bogus row */
                    continue;
                }
                uint32_t id = (uint32_t)atoi(idbuf);
                if (id > highest_id) { highest_id = id; }

                /* Name field */
                const char *name_start = c1 + 1;
                char *c2 = strchr(name_start, ',');
                if (!c2) { continue; }
                char namebuf[CSV_MAX_LINE_LENGTH];
                size_t namelen = (size_t)(c2 - name_start);
                if (namelen >= sizeof(namebuf)) { namelen = sizeof(namebuf) - 1; }
                memcpy(namebuf, name_start, namelen); namebuf[namelen] = '\0';
                trim_spaces(namebuf);
                if (strcmp(namebuf, "Name") == 0) {
                    /* Robust skip for duplicated header rows that were parsed as data previously */
                    continue;
                }

                /* If this token is now recognized in any loaded CSV cache, skip it */
                if (csv_token_exists_in_any_csv(manager, namebuf)) {
                    if (id > highest_id) { highest_id = id; }
                    removed_recognized++;
                    /* Do not write this line (or its split parts) to the new file */
                    continue;
                }

                /* Remaining columns: Description,Filename,PackID,Pretty */
                const char *rest = c2 + 1;
                /* Extract Description up to next comma */
                char *c3 = strchr(rest, ',');
                if (!c3) { continue; }
                size_t desc_len = (size_t)(c3 - rest);
                char descbuf[CSV_MAX_LINE_LENGTH];
                if (desc_len >= sizeof(descbuf)) { desc_len = sizeof(descbuf) - 1; }
                memcpy(descbuf, rest, desc_len); descbuf[desc_len] = '\0';

                /* Filename */
                const char *fname_start = c3 + 1;
                char *c4 = strchr(fname_start, ',');
                if (!c4) { continue; }
                size_t fname_len = (size_t)(c4 - fname_start);
                char fnamebuf[CSV_MAX_LINE_LENGTH];
                if (fname_len >= sizeof(fnamebuf)) { fname_len = sizeof(fnamebuf) - 1; }
                memcpy(fnamebuf, fname_start, fname_len); fnamebuf[fname_len] = '\0';

                /* PackID */
                const char *pack_start = c4 + 1;
                char *c5 = strchr(pack_start, ',');
                if (!c5) { /* if pretty missing, assume end of line */ c5 = strchr(pack_start, '\n'); }
                char packbuf[16];
                unsigned pack_id = 0U;
                if (c5) {
                    size_t pack_len = (size_t)(c5 - pack_start);
                    if (pack_len >= sizeof(packbuf)) { pack_len = sizeof(packbuf) - 1; }
                    memcpy(packbuf, pack_start, pack_len); packbuf[pack_len] = '\0';
                    pack_id = (unsigned)atoi(packbuf);
                }

                /* Additional cleanup: If this row comes from a filename that contains
                 * an adjacent token pair forming a known contributors.csv phrase,
                 * drop this row. This fixes cases like Kernal + Version which should map
                 * to contributors token 'Kernal_Version'.
                 */
                if (fnamebuf[0] != '\0') {
                    /* Extract tokens from filename (without extension) and try two-token windows */
                    char fname_copy[CSV_MAX_LINE_LENGTH];
                    strncpy(fname_copy, fnamebuf, sizeof(fname_copy) - 1);
                    fname_copy[sizeof(fname_copy) - 1] = '\0';
                    char *dot = strrchr(fname_copy, '.');
                    if (dot) { *dot = '\0'; }
                    char *saveptr = NULL;
                    char *tok = whd_strtok_r(fname_copy, "_", &saveptr);
                    char *prev = NULL;
                    while (tok) {
                        if (prev) {
                            char joined[CSV_MAX_TOKEN_LENGTH];
                            size_t l1 = strlen(prev), l2 = strlen(tok);
                            if (l1 + 1 + l2 < sizeof(joined)) {
                                strcpy(joined, prev);
                                joined[l1] = '_';
                                strcpy(joined + l1 + 1, tok);
                                /* If contributors.csv contains this joined token, and current name matches
                                 * either side of the pair, then this special row is redundant. */
                                if (csv_cache_lookup(manager, "contributors", joined) > 0) {
                                    if (strcmp(namebuf, prev) == 0 || strcmp(namebuf, tok) == 0) {
                                        /* Skip this line entirely */
                                        prev = tok;
                                        goto next_line_rewrite; /* continue outer fgets loop */
                                    }
                                }
                            }
                        }
                        prev = tok;
                        tok = whd_strtok_r(NULL, "_", &saveptr);
                    }
                }

                /* Write header lazily on first data row */
                if (!header_written) {
                    fprintf(out, "ID,Name,Description,Filename,PackID,Pretty\n");
                    header_written = true;
                }
                /* Write one or many lines for this entry (split on '&') using the same ID */
                write_split_special_lines(out, id, namebuf, descbuf, fnamebuf, pack_id);
                wrote_any = true;
next_line_rewrite: ;
            }
            whd_fclose(in);
        }
    }

    /* Now append new unknown tokens, if any */
    uint32_t added_count = 0;
    for (uint32_t i = 0; i < manager->unknown_count; i++) {
        const char *tok = manager->unknown_tokens[i];
        if (!tok) { continue; }
        /* Skip if original (unsplit) token already exists in loaded special cache */
        if (csv_cache_is_token_in_special(manager, tok)) {
            manager->duplicates_skipped++;
            continue;
        }

        /* Also skip if this token is now recognized by any loaded CSV cache */
        if (csv_token_exists_in_any_csv(manager, tok)) {
            manager->duplicates_skipped++;
            continue;
        }

        /* Allocate new ID for this token, used for all its split parts */
        highest_id++;

        /* Description for new entries */
        char descbuf[CSV_MAX_LINE_LENGTH];
        snprintf(descbuf, sizeof(descbuf), "Unknown token from %s", manager->unknown_filenames[i]);

        unsigned pack_id = manager->unknown_pack_ids ? (unsigned)manager->unknown_pack_ids[i] : 0U;

        /* Write header lazily on first data row */
        if (!header_written) {
            fprintf(out, "ID,Name,Description,Filename,PackID,Pretty\n");
            header_written = true;
        }
        /* Write split lines for this new token */
        write_split_special_lines(out, highest_id, tok, descbuf, manager->unknown_filenames[i], pack_id);
        added_count++;
        wrote_any = true;
    }

    whd_fclose(out);

    /* If nothing was written, replace with a comment-only file */
    if (!wrote_any) {
        out = whd_fopen(tmp_path, "w");
        if (out) {
            fprintf(out, "# No unknown tokens found - all filename tokens matched existing CSV definitions\n");
            whd_fclose(out);
        }
    }

    /* Replace original file with temp */
    remove(special_csv_path);
    rename(tmp_path, special_csv_path);

#if PLATFORM_AMIGA
    append_to_log("Updated special.csv: %u new tokens added, %u duplicates skipped",
                 (unsigned)added_count, (unsigned)manager->duplicates_skipped);
#endif

    return true;
}

/*------------------------------------------------------------------------*/
/* Statistics and Monitoring */

/**
 * @brief Get memory usage statistics for monitoring
 */
void csv_cache_get_memory_stats(const GlobalCSVManager *manager,
                               uint32_t *total_kb,
                               uint32_t *limit_kb,
                               bool *cache_enabled) {
    if (!manager) {
        return;
    }

    if (total_kb) {
        *total_kb = manager->total_memory_kb;
    }
    if (limit_kb) {
        *limit_kb = manager->memory_limit_kb;
    }
    if (cache_enabled) {
        *cache_enabled = manager->cache_enabled;
    }
}

/**
 * @brief Generate processing summary including missing CSV report
 */
void csv_cache_report_summary(const GlobalCSVManager *manager) {
    if (!manager) {
        return;
    }

#if PLATFORM_AMIGA
    if (manager->missing_csv_count > 0) {
        append_to_log("PROCESSING SUMMARY:");
    append_to_log("  Loaded CSVs: %lu", (unsigned long)manager->cache_count);
    append_to_log("  Missing CSVs: %lu", (unsigned long)manager->missing_csv_count);
        append_to_log("  Cache mode: %s", manager->cache_enabled ? "ENABLED" : "DIRECT_ACCESS");
    append_to_log("  Memory usage: %luKB / %luKB", (unsigned long)manager->total_memory_kb, (unsigned long)manager->memory_limit_kb);

        for (uint32_t i = 0; i < manager->missing_csv_count; i++) {
            append_to_log("    Missing: %s.csv", manager->missing_csv_names[i]);
        }
    } else {
    append_to_log("CSV loading complete: %lu files loaded successfully", (unsigned long)manager->cache_count);
    append_to_log("Memory usage: %luKB / %luKB", (unsigned long)manager->total_memory_kb, (unsigned long)manager->memory_limit_kb);
    }
#endif
}

/*------------------------------------------------------------------------*/

/* End of Text */
