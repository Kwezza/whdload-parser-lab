/* csv_cache.h - Global CSV Cache Manager for TLV Processing
 *
 * Copyright 2025 Topaz Systems Ltd
 * SPDX-License-Identifier: MIT
 *
 * High-performance CSV cache system optimized for 4MB Amiga systems with
 * adaptive memory management and graceful degradation when CSV files are missing.
 *
 * Key Features:
 * - Global CSV cache with O(1) hash table lookups
 * - Adaptive memory management (default 512KB limit)
 * - Automatic fallback for missing CSV files
 * - Memory-aware caching with configurable limits
 * - Cross-platform compatibility (Amiga/Host)
 *
 * Created: 2025-08-06
 */

#ifndef TLV_FILENAME_CSV_CACHE_H
#define TLV_FILENAME_CSV_CACHE_H

#pragma once

#include <platform.h>
#include <stdint.h>

/* Forward declarations */
struct AppCtx;

/*------------------------------------------------------------------------*/
/* CSV Cache Types */

/**
 * @brief Single CSV entry (token -> ID mapping)
 */
typedef struct {
    char *token;        /* The lookup key (e.g., "Cinemaware", "OCS", "En") */
    uint32_t id;        /* The CSV ID for this token */
    char *long_name;    /* Optional: full/wordy display name (e.g., "French") */
} CSVEntry;

/**
 * @brief Hash table cache for a single CSV file
 */
typedef struct {
    char *csv_name;           /* e.g., "publisher", "chipset", "language" */
    CSVEntry *entries;        /* Hash table of token->ID mappings */
    uint32_t entry_count;     /* Number of entries */
    uint32_t capacity;        /* Hash table size */
    uint32_t memory_usage_kb; /* Memory footprint of this cache */
    bool has_default_token;   /* Whether a 'default' marker was present */
    uint32_t default_token_id;/* Token id marked as default (0 if none) */
} CSVCache;

/**
 * @brief Global CSV cache manager with adaptive memory management
 */
typedef struct {
    CSVCache *caches;         /* Array of loaded CSV caches */
    uint32_t cache_count;     /* Number of loaded CSVs */
    CSVCache *special_cache;  /* Cache for special.csv duplicate checking */

    /* CSV Base Path */
    char csv_base_path[256];  /* Base directory for CSV files */

    /* Unknown Token Tracking */
    char **unknown_tokens;    /* Tokens not found in any CSV */
    char **unknown_filenames; /* Source filenames for unknown tokens */
    uint8_t *unknown_pack_ids;/* Source pack IDs for unknown tokens (0 if unknown) */
    uint32_t unknown_count;   /* Count of unknown tokens */
    uint32_t duplicates_skipped; /* Count of duplicate tokens skipped */
    uint32_t unknown_capacity; /* Allocated capacity for unknown_* arrays */

    /* Memory Management */
    uint32_t total_memory_kb;    /* Total memory used by all caches */
    uint32_t memory_limit_kb;    /* User-configurable memory limit (default: 512KB) */
    bool cache_enabled;          /* Whether caching is active */
    uint32_t missing_csv_count;  /* Count of CSV files that couldn't be loaded */
    char **missing_csv_names;    /* Names of missing CSV files for reporting */
} GlobalCSVManager;
/* Internal note: unknown_tokens/unknown_filenames arrays are dynamically grown.
 * Track capacity explicitly to avoid overflows during large runs. */

/*------------------------------------------------------------------------*/
/* Cache Manager Lifecycle */

/**
 * @brief Initialize CSV cache manager with configurable base path and default memory limit
 * @param manager CSV manager to initialize
 * @param ctx Application context for resource access
 * @param csv_base_path Base directory path for CSV files (e.g., "assets/amiga_sys/defs")
 * @return true if successful, false on error
 */
bool csv_cache_manager_init(GlobalCSVManager *manager, struct AppCtx *ctx, const char *csv_base_path);

/**
 * @brief Initialize CSV cache manager with specific memory configuration
 * @param manager CSV manager to initialize
 * @param ctx Application context for resource access
 * @param csv_base_path Base directory path for CSV files
 * @param memory_limit_kb Memory limit in kilobytes
 * @return true if successful, false on error
 */
bool csv_cache_manager_init_with_config(GlobalCSVManager *manager,
                                       struct AppCtx *ctx,
                                       const char *csv_base_path,
                                       uint32_t memory_limit_kb);

/**
 * @brief Clean up CSV cache manager and all resources
 * @param manager CSV manager to cleanup
 */
void csv_cache_manager_cleanup(GlobalCSVManager *manager);

/*------------------------------------------------------------------------*/
/* CSV File Operations */

/**
 * @brief Load specific CSV file into cache with graceful degradation
 * @param manager CSV manager
 * @param csv_name Name of CSV to load (without .csv extension)
 * @return true if loaded, false if missing (not fatal)
 */
bool csv_cache_load_file(GlobalCSVManager *manager, const char *csv_name);

/**
 * @brief Load existing special.csv into cache for duplicate checking
 * @param manager CSV manager
 * @param special_csv_path Path to special.csv file
 * @return true if loaded, false on error
 */
bool csv_cache_load_special_csv(GlobalCSVManager *manager, const char *special_csv_path);

/*------------------------------------------------------------------------*/
/* Lookup Operations */

/**
 * @brief Fast lookup: find ID for token in specific CSV cache
 * @param manager CSV manager
 * @param csv_name CSV cache name to search
 * @param token Token to find
 * @return Token ID if found, 0 if not found
 */
uint32_t csv_cache_lookup(GlobalCSVManager *manager, const char *csv_name, const char *token);

/**
 * @brief Find which CSV file contains a specific token
 * @param manager CSV manager
 * @param token Token to locate
 * @return CSV filename containing token, or NULL if not found
 */
const char *csv_cache_find_token_source(GlobalCSVManager *manager, const char *token);

/**
 * @brief Reverse lookup: find token string for an ID in a specific CSV cache
 * @param manager CSV manager
 * @param csv_name CSV cache name to search (without .csv extension)
 * @param id Numeric ID to resolve
 * @return Pointer to token string if found, or NULL if not found
 *
 * Notes:
 * - The returned pointer is owned by the cache; do not free it.
 * - This performs a linear scan over the cache's hash table and is O(N),
 *   intended for diagnostics and preview output, not hot paths.
 */
/**
 * @brief Reverse lookup with display selection: return token (short) or long name
 * @param manager CSV manager
 * @param csv_name CSV cache name to search (without .csv extension)
 * @param id Numeric ID to resolve
 * @param want_long If true and a long name exists, return it; otherwise return the short token
 * @return Pointer to string owned by cache (or static buffer when cache disabled), or NULL
 */
const char *csv_cache_reverse_lookup(GlobalCSVManager *manager, const char *csv_name, uint32_t id, bool want_long);

/**
 * @brief Get default token id (and flag) for a CSV cache
 * @param manager CSV manager
 * @param csv_name CSV base name
 * @param has_default out flag (may be NULL)
 * @return default token id or 0 if none
 */
uint32_t csv_cache_get_default_token(GlobalCSVManager *manager, const char *csv_name, bool *has_default);

/*------------------------------------------------------------------------*/
/* Unknown Token Management */

/**
 * @brief Add token to unknown tokens list for special.csv
 * @param manager CSV manager
 * @param token Token not found in any CSV
 * @param filename Source filename for context
 * @return true if added, false if duplicate or error
 */
bool csv_cache_add_unknown_token(GlobalCSVManager *manager, const char *token, const char *filename);

/**
 * @brief Add token to unknown tokens list with explicit pack id (extended API)
 * @param manager CSV manager
 * @param token Token not found in any CSV
 * @param filename Source filename for context
 * @param pack_id Pack identifier the filename came from (0 if unknown)
 * @return true if added, false if duplicate or error
 */
bool csv_cache_add_unknown_token_ex(GlobalCSVManager *manager, const char *token, const char *filename, uint8_t pack_id);

/**
 * @brief Check if token already exists in special.csv cache
 * @param manager CSV manager
 * @param token Token to check
 * @return true if exists, false if new
 */
bool csv_cache_is_token_in_special(GlobalCSVManager *manager, const char *token);

/**
 * @brief Write unknown tokens to special.csv file
 * @param manager CSV manager
 * @param special_csv_path Path to special.csv file
 * @return true if written successfully, false on error
 */
bool csv_cache_update_special_csv(GlobalCSVManager *manager, const char *special_csv_path);

/*------------------------------------------------------------------------*/
/* Memory and Statistics */

/**
 * @brief Get memory usage and cache statistics
 * @param manager CSV manager
 * @param total_kb Total memory used in KB (output)
 * @param limit_kb Memory limit in KB (output)
 * @param cache_enabled Whether caching is active (output)
 */
void csv_cache_get_memory_stats(const GlobalCSVManager *manager,
                               uint32_t *total_kb,
                               uint32_t *limit_kb,
                               bool *cache_enabled);

/**
 * @brief Print cache statistics and summary
 * @param manager CSV manager
 */
void csv_cache_report_summary(const GlobalCSVManager *manager);

/*------------------------------------------------------------------------*/
/* Direct File Access (Fallback) */

/**
 * @brief Direct CSV file lookup when cache is disabled
 * @param csv_path Full path to CSV file
 * @param token Token to find
 * @return Token ID if found, 0 if not found
 */
uint32_t csv_direct_file_lookup(const char *csv_path, const char *token);

#endif /* TLV_FILENAME_CSV_CACHE_H */

/* End of Text */
