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

#include "platform.h"
#include <stdint.h>

/* Forward declarations */
struct AppCtx;

/*------------------------------------------------------------------------*/
/* CSV Cache Types */

/**
 * @brief Single CSV entry (token -> ID mapping)
 */
typedef struct {
    char *token;        /* The lookup key (lowercase canonical, e.g., "cinemaware", "ocs", "en") */
    uint32_t id;        /* The CSV ID for this token */
    char *long_name;    /* Optional: full/wordy display name (e.g., "French") */
    uint16_t len;       /* strlen(token) cached to reject mismatched-length candidates cheaply */
    uint16_t fingerprint; /* Low 16 bits of djb2 hash, used as a fast pre-filter before strcmp() */
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
    uint8_t min_token_count;  /* Min underscore-token count across all entries (prescan prune) */
    uint8_t max_token_count;  /* Max underscore-token count across all entries (prescan prune) */
    uint16_t min_entry_len;   /* Shortest entry's strlen (length triage gate before probe) */
    uint16_t max_entry_len;   /* Longest entry's strlen (length triage gate before probe) */
    uint32_t crc32;           /* CRC-32/ISO-HDLC of the raw CSV file bytes as loaded */
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

/* csv_cache_load_special_csv: declaration removed in Phase 2 header hygiene
 * (2026-05-11). Definition retained in csv_cache.c. */

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
 * @brief Resolve a CSV cache by name, loading it on demand when caching is enabled
 * @param manager CSV manager
 * @param csv_name CSV cache name to resolve
 * @return Loaded cache pointer, or NULL if unavailable
 */
CSVCache *csv_cache_get_or_load(GlobalCSVManager *manager, const char *csv_name);

/**
 * @brief Fast lookup against an already resolved CSV cache
 * @param cache Loaded CSV cache
 * @param token Token to find
 * @return Token ID if found, 0 if not found
 */
uint32_t csv_cache_lookup_loaded(const CSVCache *cache, const char *token);

/**
 * @brief Span-based lookup: hash and compare against parts[start..start+window-1] joined by '_'
 * without materialising an intermediate string. Equivalent to lookup_loaded on the joined form.
 * @param cache Loaded CSV cache
 * @param parts Array of token part pointers
 * @param start Index of first part in the span
 * @param window Number of parts in the span
 * @return Token ID if found, 0 if not found
 */
uint32_t csv_cache_lookup_span(const CSVCache *cache,
                               char *const *parts,
                               uint32_t start,
                               uint32_t window,
                               uint16_t cand_len);

/**
 * @brief Probe-only hot path: caller has already lowercased the token and computed its hash,
 * length, and fingerprint. Skips the lowercase+hash pass inside the probe loop, reusing
 * precomputed values across multiple cache calls on the same token.
 * @param cache     Loaded CSV cache
 * @param lower     Lowercase canonical form of the token (null-terminated)
 * @param look_len  strlen(lower), pre-cached to avoid repeated strlen calls
 * @param look_fp   Low 16 bits of djb2 hash (cheap pre-filter before strcmp)
 * @param raw_hash  Full 32-bit djb2 hash (used for slot indexing)
 * @return Token ID if found, 0 if not found
 */
uint32_t csv_cache_lookup_prehashed(const CSVCache *cache,
                                    const char *lower,
                                    uint16_t look_len,
                                    uint16_t look_fp,
                                    uint32_t raw_hash);

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
 * @brief Add token to unknown tokens list with explicit pack id (extended API)
 * @param manager CSV manager
 * @param token Token not found in any CSV
 * @param filename Source filename for context
 * @param pack_id Pack identifier the filename came from (0 if unknown)
 * @return true if added, false if duplicate or error
 */
bool csv_cache_add_unknown_token_ex(GlobalCSVManager *manager, const char *token, const char *filename, uint8_t pack_id);

/* csv_cache_update_special_csv: declaration removed in Phase 2 header hygiene
 * (2026-05-11). Definition retained in csv_cache.c. */

/*------------------------------------------------------------------------*/
/* Memory and Statistics */

/* csv_cache_get_memory_stats, csv_cache_report_summary: declarations removed in
 * Phase 2 header hygiene (2026-05-11). Definitions retained in csv_cache.c. */

/*------------------------------------------------------------------------*/
/* Direct File Access (Fallback) */

/**
 * @brief Print diagnostic counters for the CSV cache (profiling builds only)
 * @param stream Output file stream
 *
 * Currently reports: csv_find_ci_calls (number of times the case-insensitive
 * fallback fired). Should be zero after case canonicalisation is in place.
 */
void csv_cache_print_stats(FILE *stream);

/*------------------------------------------------------------------------*/
/* CRC Fingerprint Access */

/* csv_cache_get_crc: declaration removed in Phase 2 header hygiene
 * (2026-05-11). Definition retained in csv_cache.c. */

#endif /* TLV_FILENAME_CSV_CACHE_H */

/* End of Text */
