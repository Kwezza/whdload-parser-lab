/* filename_processor.c - TLV Filename Processing Orchestrator Implementation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Main orchestration engine for processing WHDLoad package filenames into
 * structured TLV metadata using dynamic field registry and CSV cache system.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#include "platform.h"
#include "whdtlv/core/filename_processor.h"
#include "whdtlv/core/error_handling.h"
#include "whdtlv/core/tlv_builder.h"
#include "whdtlv/core/tlv_profile.h"
#include "whdtlv/core/field_registry.h"
#include "whdtlv/core/csv_cache.h"
#include "whdtlv/io/writeLog.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "whdtlv/platform/platform_string.h"

/*------------------------------------------------------------------------*/
/* Constants */

#define MAX_FILENAME_LENGTH         512
#define MAX_TOKEN_LENGTH            128
#define MAX_PROGRAM_NAME_LENGTH     256
#define MAX_VERSION_LENGTH          64
#define MAX_TOKENS                  32
#define MAX_LANGUAGE_CHARS          16

/*------------------------------------------------------------------------*/
/* Internal Structures */

/**
 * @brief Tokenized filename data
 */
typedef struct {
    char program_name[MAX_PROGRAM_NAME_LENGTH];
    char **tokens;
    uint32_t token_count;
    uint32_t tokens_allocated;
} TokenizedFilename;

/*------------------------------------------------------------------------*/
/* Helper Functions */

/**
 * @brief Allocate and initialize tokenized filename structure
 */
static TokenizedFilename *tokenized_filename_alloc(void) {
    TokenizedFilename *tf = whd_malloc(sizeof(TokenizedFilename));
    if (!tf) {
        return NULL;
    }

    memset(tf, 0, sizeof(TokenizedFilename));
    tf->tokens = whd_malloc(MAX_TOKENS * sizeof(char *));
    if (!tf->tokens) {
        whd_free(tf);
        return NULL;
    }

    tf->tokens_allocated = MAX_TOKENS;
    return tf;
}

/**
 * @brief Free tokenized filename structure
 */
static void tokenized_filename_free(TokenizedFilename *tf) {
    if (!tf) {
        return;
    }

    if (tf->tokens) {
        for (uint32_t i = 0; i < tf->token_count; i++) {
            if (tf->tokens[i]) {
                whd_free(tf->tokens[i]);
            }
        }
        whd_free(tf->tokens);
    }

    whd_free(tf);
}

/**
 * @brief Add token to tokenized filename
 */
static bool tokenized_filename_add_token(TokenizedFilename *tf, const char *token) {
    if (!tf || !token || tf->token_count >= tf->tokens_allocated) {
        return false;
    }

    tf->tokens[tf->token_count] = whd_malloc(strlen(token) + 1);
    if (!tf->tokens[tf->token_count]) {
        return false;
    }

    strcpy(tf->tokens[tf->token_count], token);
    tf->token_count++;
    return true;
}

/**
 * @brief Build an underscore-joined token span into a fixed buffer
 */
static bool build_joined_token(char *buffer,
                               size_t buffer_size,
                               char *const *parts,
                               uint32_t start,
                               uint32_t window) {
    size_t length;

    if (!buffer || buffer_size == 0 || !parts || window == 0) {
        return false;
    }

    length = 0;
    buffer[0] = '\0';

    for (uint32_t k = 0; k < window; k++) {
        const char *part = parts[start + k];
        size_t part_length;

        if (!part) {
            return false;
        }

        part_length = strlen(part);
        if ((k > 0 && length + 1 >= buffer_size) ||
            part_length >= buffer_size ||
            length + part_length >= buffer_size) {
            buffer[0] = '\0';
            return false;
        }

        if (k > 0) {
            buffer[length++] = '_';
        }

        memcpy(buffer + length, part, part_length);
        length += part_length;
        buffer[length] = '\0';
    }

    return true;
}

/**
 * @brief Rebuild an underscore-joined filename from token parts
 */
static void rebuild_filename_from_parts(char *buffer,
                                        size_t buffer_size,
                                        char *const *parts,
                                        uint32_t part_count) {
    uint32_t index;
    size_t length;

    if (!buffer || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!parts || part_count == 0) {
        return;
    }

    length = 0;
    for (index = 0; index < part_count; index++) {
        const char *part;
        size_t part_length;

        part = parts[index];
        if (!part || part[0] == '\0') {
            continue;
        }

        part_length = strlen(part);
        if (length > 0) {
            if (length + 1 >= buffer_size) {
                break;
            }
            buffer[length++] = '_';
        }

        if (length + part_length >= buffer_size) {
            break;
        }

        memcpy(buffer + length, part, part_length);
        length += part_length;
        buffer[length] = '\0';
    }
}

/**
 * @brief Remove a contiguous token span by compacting part pointers in place
 */
static void compact_token_parts(char **parts,
                                uint32_t *part_count,
                                uint32_t start,
                                uint32_t window) {
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;

    if (!parts || !part_count || window == 0) {
        return;
    }

    count = *part_count;
    if (start >= count || start + window > count) {
        return;
    }

    write_index = start;
    for (read_index = start + window; read_index < count; read_index++) {
        parts[write_index++] = parts[read_index];
    }

    *part_count = count - window;
}

/**
 * @brief Cheap filter for version-like tokens before invoking the parser
 */
static bool token_might_be_version(const char *token) {
    unsigned char first;

    if (!token || token[0] == '\0') {
        return false;
    }

    first = (unsigned char)token[0];
    if (tolower(first) == 'v') {
        return isdigit((unsigned char)token[1]) ? true : false;
    }

    return (isdigit(first) && strchr(token, '.') != NULL) ? true : false;
}

/**
 * @brief Cheap filter for language-like tokens before invoking the parser
 */
static bool token_might_be_language(const char *token) {
    size_t length;
    size_t index;

    if (!token || token[0] == '\0') {
        return false;
    }

    length = strlen(token);
    if (length < 2 || length > MAX_LANGUAGE_CHARS || (length % 2) != 0) {
        return false;
    }

    for (index = 0; index < length; index++) {
        if (!isalpha((unsigned char)token[index])) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Cheap filter for numeric SPS tokens before scanning parts
 */
static bool token_might_be_sps(const char *token) {
    unsigned char first;

    if (!token || token[0] == '\0') {
        return false;
    }

    first = (unsigned char)token[0];
    return isdigit(first) ? true : false;
}

/**
 * @brief Lowercase token into lower_out and compute djb2 hash, length, and fingerprint in one
 * pass.  lower_out must be at least MAX_TOKEN_LENGTH bytes.  Tokens longer than
 * MAX_TOKEN_LENGTH-1 are silently truncated; the subsequent lookup will produce a miss, which
 * is safe.
 */
static void token_compute_prehash(const char *token,
                                  char *lower_out,
                                  uint32_t *out_raw_hash,
                                  uint16_t *out_len,
                                  uint16_t *out_fp)
{
    uint32_t h = 5381;
    uint16_t l = 0;
    const char *p = token;
    char *q = lower_out;

    while (*p && l < (uint16_t)(MAX_TOKEN_LENGTH - 1)) {
        unsigned char c  = (unsigned char)*p;
        unsigned char lc = (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
        *q = (char)lc;
        h = ((h << 5) + h) + (uint32_t)lc;
        p++; q++; l++;
    }
    *q = '\0';
    *out_raw_hash = h;
    *out_len = l;
    *out_fp  = (uint16_t)(h & 0xFFFFU);
}

/*------------------------------------------------------------------------*/
/* Step B: per-field pack match hit counters (profiling builds only) */

#if TLV_PROFILE_ENABLE
#define PACK_FIELD_PROFILE_MAX_SLOTS 64
static uint32_t g_pack_field_hits[PACK_FIELD_PROFILE_MAX_SLOTS];
static const char *g_pack_field_names[PACK_FIELD_PROFILE_MAX_SLOTS];
static uint32_t g_pack_field_slot_count = 0;

static void pack_field_record_hit(const char *field_name)
{
    uint32_t i;
    if (!field_name) {
        return;
    }
    for (i = 0; i < g_pack_field_slot_count; i++) {
        if (g_pack_field_names[i] && strcmp(g_pack_field_names[i], field_name) == 0) {
            g_pack_field_hits[i]++;
            return;
        }
    }
    if (g_pack_field_slot_count < PACK_FIELD_PROFILE_MAX_SLOTS) {
        g_pack_field_names[g_pack_field_slot_count] = field_name;
        g_pack_field_hits[g_pack_field_slot_count]  = 1;
        g_pack_field_slot_count++;
    }
}
#endif /* TLV_PROFILE_ENABLE */

void filename_processor_print_pack_field_stats(FILE *stream)
{
#if TLV_PROFILE_ENABLE
    uint32_t i;
    if (!stream) {
        return;
    }
    fprintf(stream, "pack_field_hit_counts       (%lu unique fields):\n",
            (unsigned long)g_pack_field_slot_count);
    for (i = 0; i < g_pack_field_slot_count; i++) {
        fprintf(stream, "  %-28s = %lu\n",
                g_pack_field_names[i] ? g_pack_field_names[i] : "(null)",
                (unsigned long)g_pack_field_hits[i]);
    }
#else
    (void)stream;
#endif
}

/*------------------------------------------------------------------------*/
/* Filename Sanitization */

/**
 * @brief Sanitize filename (remove extension, normalize format)
 */
static ProcessingResult filename_sanitizer_process(const char *input,
                                          char *output,
                                          ProcessingError *error) {
    if (!input || !output) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Input or output parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Copy input to output safely (avoid strncpy truncation warning) */
    {
        size_t in_len = strlen(input);
        if (in_len >= (size_t)MAX_FILENAME_LENGTH) {
            in_len = (size_t)MAX_FILENAME_LENGTH - 1;
        }
        memcpy(output, input, in_len);
        output[in_len] = '\0';
    }

    /* Find and remove extension */
    char *dot = strrchr(output, '.');
    if (dot) {
        *dot = '\0';
    }

    /* Basic validation */
    if (strlen(output) == 0) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, input, NULL,
                             "Filename is empty after sanitization");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Version Parsing */

/**
 * @brief Detect if token contains version pattern
 */
ProcessingResult version_parser_detect_pattern(const char *token,
                                              ProcessingError *error) {
    if (!token) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Token parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Check for version patterns: v1.0, v1.2a, v2.1.3, etc. */
    if (tolower(token[0]) == 'v' && isdigit(token[1])) {
        return PROCESSING_SUCCESS;
    }

    /* Check for bare version patterns: 1.0, 2.1, etc. */
    if (isdigit(token[0]) && strchr(token, '.')) {
        return PROCESSING_SUCCESS;
    }

    return PROCESSING_ERROR_TOKEN_NOT_FOUND;
}

/**
 * @brief Parse version token and extract clean version
 */
static ProcessingResult version_parser_extract(const char *version_token,
                                       char *clean_version,
                                       ProcessingError *error) {
    if (!version_token || !clean_version) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Input parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Skip 'v' prefix if present */
    const char *start = version_token;
    if (tolower(start[0]) == 'v') {
        start++;
    }

    /* Copy version string safely */
    {
        size_t vlen = strlen(start);
        if (vlen >= (size_t)MAX_VERSION_LENGTH) {
            vlen = (size_t)MAX_VERSION_LENGTH - 1;
        }
        memcpy(clean_version, start, vlen);
        clean_version[vlen] = '\0';
    }

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Language Parsing */

/**
 * @brief Validate a compact language token and build its bitfield.
 *
 * A token is valid ONLY if it can be split into 2-character chunks that
 * ALL resolve in Language.csv.  If any chunk is unknown the whole token is
 * rejected: no bits are set and the function returns false.
 *
 * This rule prevents substring extraction from arbitrary words such as
 * "EasyPlay" (which contains "Pl" = Polish) or "Infogrames" (which
 * contains "Gr" = Greek and "Es" = Spanish).
 *
 * Single-language tokens (len == 2): one chunk must match exactly.
 * Compact multilingual tokens (len >= 4, even): every chunk must match.
 *
 * C89-compatible: all locals declared at block top, no for-init syntax.
 */
static bool is_compact_language_token(const char *token,
                                      CSVCache *language_cache,
                                      GlobalCSVManager *csv_manager,
                                      uint16_t *out_bitfield)
{
    size_t len;
    size_t i;

    *out_bitfield = 0;

    len = strlen(token);
    /* Odd length or empty: cannot be made of 2-character chunks */
    if (len < 2 || (len % 2) != 0 || len > (size_t)MAX_LANGUAGE_CHARS) {
        return false;
    }

    for (i = 0; i < len; i += 2) {
        char c0 = token[i];
        char c1 = token[i + 1];
        char lang_code[3];
        uint32_t lang_id;

        if (c0 >= 'A' && c0 <= 'Z') { c0 = (char)(c0 + 32); }
        if (c1 >= 'A' && c1 <= 'Z') { c1 = (char)(c1 + 32); }
        lang_code[0] = c0;
        lang_code[1] = c1;
        lang_code[2] = '\0';

        if (language_cache != NULL) {
            lang_id = csv_cache_lookup_loaded(language_cache, lang_code);
        } else {
            lang_id = csv_cache_lookup(csv_manager, "Language", lang_code);
        }

        /* Unknown chunk: reject the entire token immediately */
        if (lang_id == 0 || lang_id > 16) {
            *out_bitfield = 0;
            return false;
        }

        *out_bitfield |= (uint16_t)(1u << (lang_id - 1u));
    }

    return true;
}

/**
 * @brief Parse language token into bitfield using field registry
 */
static ProcessingResult language_parser_parse_token(const char *language_token,
                                            const FieldRegistry *field_registry,
                                            GlobalCSVManager *csv_manager,
                                            uint16_t *language_bitfield,
                                            ProcessingError *error) {
    CSVCache *language_cache;

    if (!language_token || !field_registry || !csv_manager || !language_bitfield) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    *language_bitfield = 0;

    language_cache = NULL;
    if (csv_manager->cache_enabled) {
        language_cache = csv_cache_get_or_load(csv_manager, "Language");
    }

    /* Validate and decode the token. Every 2-character chunk must resolve
     * in Language.csv; partial substring matches are not accepted. */
    if (!is_compact_language_token(language_token, language_cache,
                                   csv_manager, language_bitfield)) {
        processing_error_set(error, PROCESSING_ERROR_TOKEN_NOT_FOUND, language_token, NULL,
                             "Not a valid language token");
        return PROCESSING_ERROR_TOKEN_NOT_FOUND;
    }

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Contributor Extraction (legacy path retained for backward compatibility) */

/**
 * @brief Extract multi-token contributors from filename
 */
ProcessingResult contributor_extractor_process(const char *filename,
                                             const FieldRegistry *field_registry,
                                             GlobalCSVManager *csv_manager,
                                             TLV_Record *output_record,
                                             char *processed_filename,
                                             ProcessingError *error) {
    if (!filename || !field_registry || !csv_manager || !processed_filename) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Copy input to output initially */
    strcpy(processed_filename, filename);

    /* Look for multi-token contributor patterns like "German_fix_by_Torti-the-Smurf" */
    char *pos = strstr(processed_filename, "_fix_by_");
    if (!pos) {
        pos = strstr(processed_filename, "_crack_by_");
    }
    if (!pos) {
        pos = strstr(processed_filename, "_by_");
    }

    if (pos) {
        /* Find the start of the contributor phrase */
        char *start = pos;
        while (start > processed_filename && *(start - 1) != '_') {
            start--;
        }

        /* Find the end of the contributor phrase */
        char *end = pos + strlen("_fix_by_");
        while (*end != '_' && *end != '\0') {
            end++;
        }

        /* Extract contributor phrase */
        size_t phrase_len = end - start;
        char contributor_phrase[MAX_TOKEN_LENGTH];
        if (phrase_len >= sizeof(contributor_phrase)) {
            phrase_len = sizeof(contributor_phrase) - 1;
        }
        memcpy(contributor_phrase, start, phrase_len);
        contributor_phrase[phrase_len] = '\0';

        /* Look up in contributors CSV */
        uint32_t contributor_id = csv_cache_lookup(csv_manager, "contributors", contributor_phrase);
        if (contributor_id > 0) {
            /* Add contributor to TLV record */
            uint8_t contributor_field_id = field_registry_get_id(field_registry, "contributors");
            if (contributor_field_id != 0) {
                uint8_t contributor_id_be[4]; /* stored as big-endian uint32 */
                contributor_id_be[0] = (uint8_t)(contributor_id >> 24);
                contributor_id_be[1] = (uint8_t)(contributor_id >> 16);
                contributor_id_be[2] = (uint8_t)(contributor_id >>  8);
                contributor_id_be[3] = (uint8_t)(contributor_id & 0xFF);
                tlv_record_add_entry(output_record, contributor_field_id,
                                   contributor_id_be, 4);
#if PLATFORM_AMIGA
                whdtlv_log_append("Added contributor '%s' ID %lu to TLV record (field_id=0x%02X)",
                             contributor_phrase, (unsigned long)contributor_id, contributor_field_id);
#endif
            }
#if PLATFORM_AMIGA
            whdtlv_log_append("Found contributor: %s (ID=%lu)", contributor_phrase, (unsigned long)contributor_id);
#endif

            /* Remove contributor phrase from filename */
            memmove(start, end + 1, strlen(end + 1) + 1);
        }
    }

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Generic Prescan Phase */

/**
 * @brief Perform generic prescan for fields configured in [FieldAttributes]
 * Joins adjacent tokens (window 1..3) and looks up in configured CSV. On match,
 * adds TLV entries with the matched ID(s) and optionally removes the span from the filename.
 */
static ProcessingResult prescan_and_strip_tokens(const char *filename,
                                                const FieldRegistry *field_registry,
                                                GlobalCSVManager *csv_manager,
                                                TLV_Record *output_record,
                                                char *processed_filename,
                                                ProcessingError *error) {
    TLV_PROFILE_SCOPE(lookup_profile_stamp);
    TLV_PROFILE_SCOPE(rebuild_profile_stamp);
    CSVCache *cfg_caches[32];
    uint32_t cache_index;

    if (!filename || !field_registry || !csv_manager || !processed_filename) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }
    /* Start with a working copy */
    {
        size_t flen = strlen(filename);
        if (flen >= (size_t)MAX_FILENAME_LENGTH) { flen = (size_t)MAX_FILENAME_LENGTH - 1; }
        memcpy(processed_filename, filename, flen);
        processed_filename[flen] = '\0';
    }

    /* Gather prescan fields from registry */
    FieldPrescanConfig cfgs[32];
    uint32_t cfg_count = field_registry_list_prescan_fields(field_registry, cfgs, 32);
    if (cfg_count == 0) {
        return PROCESSING_SUCCESS; /* nothing to do */
    }

    for (cache_index = 0; cache_index < cfg_count; cache_index++) {
        cfg_caches[cache_index] = NULL;
        if (csv_manager->cache_enabled && cfgs[cache_index].csv_base &&
            cfgs[cache_index].csv_base[0] != '\0') {
            cfg_caches[cache_index] = csv_cache_get_or_load(csv_manager, cfgs[cache_index].csv_base);
        }
    }

    /* Pre-compute once: avoids repeated strstr on every inner-loop iteration (Issue 2) */
    int is_debug_filename = filename && (
        strstr(filename, "Kernal_Version") != NULL ||
        strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL);

    /* Targeted debug: list prescan fields for known sample names */
    if (is_debug_filename) {
    whdtlv_log_append("PRESCAN ACTIVE: %lu field(s)", (unsigned long)cfg_count);
        for (uint32_t dc = 0; dc < cfg_count; dc++) {
            whdtlv_log_append("  field=%s csv=%s order=%lu multi=%u remove=%u multival=%u",
                          cfgs[dc].field_name ? cfgs[dc].field_name : "?",
                          cfgs[dc].csv_base ? cfgs[dc].csv_base : "",
                          (unsigned long)cfgs[dc].order,
                          cfgs[dc].multi_token ? 1u : 0u,
                          cfgs[dc].remove_from_filename ? 1u : 0u,
                          cfgs[dc].allow_multiple ? 1u : 0u);
        }
    }

    /* Step 8: tokenize once, share parts[] across all fields.
     * compact_token_parts() modifies parts[] in place so each field sees prior removals
     * without re-copying or re-tokenizing processed_filename. One rebuild is done at the
     * very end, only if at least one span was actually removed. */
    char tmp[MAX_FILENAME_LENGTH];
    char *parts[MAX_TOKENS];
    uint16_t part_len[MAX_TOKENS];
    uint32_t pc = 0;
    bool any_span_removed = false;
    {
        size_t tlen = strlen(processed_filename);
        if (tlen >= sizeof(tmp)) { tlen = sizeof(tmp) - 1; }
        memcpy(tmp, processed_filename, tlen);
        tmp[tlen] = '\0';
    }
    {
        char *saveptr = NULL;
        char *tok = whd_strtok_r(tmp, "_", &saveptr);
        while (tok && pc < MAX_TOKENS) {
            parts[pc++] = tok;
            tok = whd_strtok_r(NULL, "_", &saveptr);
        }
    }
    /* Pre-compute token lengths for span length pre-screening (Issue 1) */
    {
        uint32_t pk;
        for (pk = 0; pk < pc; pk++) {
            part_len[pk] = (uint16_t)strlen(parts[pk]);
        }
    }

    /* For each configured field in order, attempt matches */
    for (uint32_t c = 0; c < cfg_count; c++) {
        const FieldPrescanConfig *cfg = &cfgs[c];
        bool field_changed;
        if (!cfg->enabled || !cfg->csv_base || cfg->csv_base[0] == '\0') {
            continue;
        }

        /* any_found reserved for future logging; avoid unused on host */
#if PLATFORM_AMIGA
        bool any_found = false;
#endif

        do {
            uint32_t max_window;
            bool removed_span;

            field_changed = false;
            removed_span = false;
            max_window = cfg->multi_token ? pc : 1U;

            /* Issue 4: tighten window range to [wmin, wmax] so the loop never visits
             * out-of-range window sizes at all, eliminating the per-iteration prune guard. */
            {
                uint32_t wmax, wmin;
                if (cfg_caches[c] != NULL) {
                    wmax = (pc > (uint32_t)cfg_caches[c]->max_token_count)
                           ? (uint32_t)cfg_caches[c]->max_token_count : pc;
                    wmin = (uint32_t)cfg_caches[c]->min_token_count;
                    if (wmin < 1U) { wmin = 1U; }
                } else {
                    wmax = max_window;
                    wmin = 1U;
                }

            for (uint32_t window = wmax; window >= wmin; window--) {
                if (pc < window) {
                    continue;
                }

                for (uint32_t i = 0; i + window <= pc; i++) {
                /* Pre-screen by span length: O(window) additions replaces O(N) hash loop
                 * for the ~73% of candidates rejected by the length gate (Issue 1). */
                uint16_t cand_len = 0;
                if (cfg_caches[c] != NULL) {
                    uint32_t cl = window - 1U;
                    uint32_t kk;
                    for (kk = i; kk < i + window; kk++) { cl += (uint32_t)part_len[kk]; }
                    if (cl < (uint32_t)cfg_caches[c]->min_entry_len ||
                        cl > (uint32_t)cfg_caches[c]->max_entry_len) {
                        continue;
                    }
                    cand_len = (uint16_t)cl;
                }
                /* Span-based lookup: hash and compare directly against parts[i..i+window-1]
                 * without materialising an intermediate joined string (Step 7). */
                TLV_PROFILE_START(lookup_profile_stamp);
                uint32_t id;
                if (cfg_caches[c] != NULL) {
                    id = csv_cache_lookup_span(cfg_caches[c], parts, i, window, cand_len);
                } else {
                    /* Cold path: no pre-resolved cache — materialise and use slow lookup */
                    char joined[MAX_TOKEN_LENGTH];
                    if (!build_joined_token(joined, sizeof(joined), parts, i, window) ||
                        joined[0] == '\0') {
                        TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_CSV_LOOKUP, lookup_profile_stamp);
                        continue;
                    }
                    id = csv_cache_lookup(csv_manager, cfg->csv_base, joined);
                }
                TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_CSV_LOOKUP, lookup_profile_stamp);

                /* Debug: materialise joined string only for known debug filenames */
                if (is_debug_filename) {
                    char dbg_joined[MAX_TOKEN_LENGTH];
                    build_joined_token(dbg_joined, sizeof(dbg_joined), parts, i, window);
                    whdtlv_log_append("PRESCAN TRY: field=%s csv=%s window=%lu token='%s' id=%lu",
                                  cfg->field_name ? cfg->field_name : "?",
                                  cfg->csv_base ? cfg->csv_base : "",
                                  (unsigned long)window,
                                  dbg_joined,
                                  (unsigned long)id);
                }

                    if (id > 0) {
                    /* Add TLV entry storing the ID */
                    if (cfg->field_id != 0) {
                        uint8_t id_be[4]; /* stored as big-endian uint32 */
                        id_be[0] = (uint8_t)(id >> 24);
                        id_be[1] = (uint8_t)(id >> 16);
                        id_be[2] = (uint8_t)(id >>  8);
                        id_be[3] = (uint8_t)(id & 0xFF);
                        tlv_record_add_entry(output_record, cfg->field_id,
                                             id_be, 4);
                    }
                    /* Targeted, low-noise debug for known sample names */
                    if (is_debug_filename) {
                        char dbg_joined[MAX_TOKEN_LENGTH];
                        build_joined_token(dbg_joined, sizeof(dbg_joined), parts, i, window);
                        whdtlv_log_append("PRESCAN MATCH: field=%s token='%s' id=%lu", cfg->field_name ? cfg->field_name : "?", dbg_joined, (unsigned long)id);
                    }
                    /* mark that we matched; reserved for Amiga-only logging */
#if PLATFORM_AMIGA
                        any_found = true;
#endif
                    if (!cfg->allow_multiple) {
                        /* If single-only, avoid repeated additions in this field */
                        /* continue scanning to remove from filename if requested below */
                    }

                    if (cfg->remove_from_filename) {
                        compact_token_parts(parts, &pc, i, window);
                        /* Mirror the compaction in part_len[] to keep lengths in sync (Issue 1) */
                        {
                            uint32_t pk;
                            for (pk = i; pk < pc; pk++) {
                                part_len[pk] = part_len[pk + window];
                            }
                        }
                        field_changed = true;
                        removed_span = true;
                        any_span_removed = true;
                        if (is_debug_filename) {
                            char debug_rebuild[MAX_FILENAME_LENGTH];
                            rebuild_filename_from_parts(debug_rebuild, sizeof(debug_rebuild), parts, pc);
                            whdtlv_log_append("PRESCAN STRIP: field=%s result='%s'", cfg->field_name ? cfg->field_name : "?", debug_rebuild);
                        }
                        break;
                    }
                }

                    if (removed_span) {
                        break;
                    }
                }

                if (removed_span) {
                    break;
                }
            }
            } /* end wmax/wmin block (Issue 4) */

        } while (pc > 0 && cfg->remove_from_filename && field_changed);
        /* No per-field rebuild: the next field reads parts[] directly (Step 8). */
    }

    /* Single rebuild after all fields: only when at least one span was removed. */
    if (any_span_removed) {
        TLV_PROFILE_START(rebuild_profile_stamp);
        rebuild_filename_from_parts(processed_filename, MAX_FILENAME_LENGTH, parts, pc);
        TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_REBUILD_STRIP, rebuild_profile_stamp);
    }

    return PROCESSING_SUCCESS;
}
/* CSV Token Matching */

/**
 * @brief Match token against specific CSV using dynamic field registry
 */
static ProcessingResult csv_token_matcher_lookup(const char *token,
                                        const char *csv_name,
                                        const FieldRegistry *field_registry,
                                        GlobalCSVManager *csv_manager,
                                        uint32_t *token_id,
                                        ProcessingError *error) {
    if (!token || !csv_name || !field_registry || !csv_manager || !token_id) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    *token_id = csv_cache_lookup(csv_manager, csv_name, token);
    if (*token_id == 0) {
        processing_error_set(error, PROCESSING_ERROR_TOKEN_NOT_FOUND, token, NULL,
                             "Token not found in CSV '%s'", csv_name);
        return PROCESSING_ERROR_TOKEN_NOT_FOUND;
    }

    return PROCESSING_SUCCESS;
}

/**
 * @brief Find which CSV contains the given token using field registry
 */
ProcessingResult csv_token_matcher_find_source(const char *token,
                                              const FieldRegistry *field_registry,
                                              GlobalCSVManager *csv_manager,
                                              const char **csv_source,
                                              uint32_t *token_id,
                                              ProcessingError *error) {
    if (!token || !field_registry || !csv_manager || !csv_source || !token_id) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    *csv_source = csv_cache_find_token_source(csv_manager, token);
    if (!*csv_source) {
        processing_error_set(error, PROCESSING_ERROR_TOKEN_NOT_FOUND, token, NULL,
                             "Token not found in any loaded CSV");
        return PROCESSING_ERROR_TOKEN_NOT_FOUND;
    }

    *token_id = csv_cache_lookup(csv_manager, *csv_source, token);
    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Filename Tokenization */

/**
 * @brief Tokenize filename by underscores and extract program name
 */
static ProcessingResult tokenize_filename(const char *filename,
                                        TokenizedFilename *tf,
                                        ProcessingError *error) {
    if (!filename || !tf) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    char *filename_copy = whd_malloc(strlen(filename) + 1);
    if (!filename_copy) {
        processing_error_set(error, PROCESSING_ERROR_MEMORY_ALLOCATION, NULL, NULL,
                             "Failed to allocate memory for filename copy");
        return PROCESSING_ERROR_MEMORY_ALLOCATION;
    }
    strcpy(filename_copy, filename);

    /* Split by underscores (reentrant tokenizer for portability) */
    char *saveptr = NULL;
    char *token = whd_strtok_r(filename_copy, "_", &saveptr);
    bool first_token = true;

    while (token && tf->token_count < tf->tokens_allocated) {
        if (first_token) {
            /* First token is the program name */
            strncpy(tf->program_name, token, sizeof(tf->program_name) - 1);
            tf->program_name[sizeof(tf->program_name) - 1] = '\0';
            first_token = false;
        } else {
            /* Add remaining tokens for processing */
            if (!tokenized_filename_add_token(tf, token)) {
                whd_free(filename_copy);
                processing_error_set(error, PROCESSING_ERROR_MEMORY_ALLOCATION, token, NULL,
                                     "Failed to add token to list");
                return PROCESSING_ERROR_MEMORY_ALLOCATION;
            }
        }
    token = whd_strtok_r(NULL, "_", &saveptr);
    }

    whd_free(filename_copy);
    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Main Orchestrator */

/**
 * @brief Main orchestrator function with comprehensive error handling
 */
ProcessingResult tlv_process_filename_orchestrator(const char *filename,
                                                 const PackType *pack_info,
                                                 const FieldRegistry *field_registry,
                                                 GlobalCSVManager *csv_manager,
                                                 const PackFieldMatcher *pack_matchers,
                                                 uint32_t pack_matcher_count,
                                                 TLV_Record *output_record,
                                                 ProcessingError *error_summary) {
    ProcessingError step_error;
    char sanitized_filename[MAX_FILENAME_LENGTH];
    char processed_filename[MAX_FILENAME_LENGTH];
    TokenizedFilename *tf = NULL;
    ProcessingResult result;
    TLV_PROFILE_SCOPE(sanitize_profile_stamp);
    TLV_PROFILE_SCOPE(prescan_profile_stamp);
    TLV_PROFILE_SCOPE(tokenize_profile_stamp);
    TLV_PROFILE_SCOPE(token_loop_profile_stamp);
    TLV_PROFILE_SCOPE(token_checks_profile_stamp);
    TLV_PROFILE_SCOPE(pack_field_match_profile_stamp);
    TLV_PROFILE_SCOPE(unknown_token_profile_stamp);
    uint8_t version_field_id;
    uint8_t language_field_id;
    uint8_t sps_field_id;

    /* Initialize error context */
    processing_error_init(error_summary, "tlv_orchestrator");

    if (!filename || !pack_info || !field_registry || !csv_manager) {
        processing_error_set(error_summary, PROCESSING_ERROR_INVALID_INPUT, NULL, filename,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    version_field_id = field_registry_get_id(field_registry, "version");
    language_field_id = field_registry_get_id(field_registry, "language");
    sps_field_id = field_registry_get_id(field_registry, "sps");

    /* Step 0: raw filename externalized to sidecar; we now emit display_name (prettified) field
     * early to serve as a record delimiter. For now we map sanitized filename directly; future
     * enhancement can integrate prettify_title() for nicer spacing/casing. */

    /* Step 1: Sanitize filename */
    processing_error_init(&step_error, "filename_sanitizer");
    TLV_PROFILE_START(sanitize_profile_stamp);
    result = filename_sanitizer_process(filename, sanitized_filename, &step_error);
    TLV_PROFILE_END(TLV_PROFILE_SECTION_SANITIZE, sanitize_profile_stamp);
    if (result != PROCESSING_SUCCESS) {
        *error_summary = step_error;
        return result;
    }

    /* Emit display_name TLV entry (acts as record boundary). Use sanitized filename directly. */
    {
        uint8_t display_id = field_registry_get_id(field_registry, "display_name");
        if (display_id != 0) {
            size_t name_len = strlen(sanitized_filename);
            if (name_len > 0 && name_len < 65535) {
                tlv_record_add_entry(output_record, display_id, (const uint8_t*)sanitized_filename, (uint16_t)name_len);
            }
        }
    }

#if PLATFORM_AMIGA
    whdtlv_log_append("Processing filename: %s", sanitized_filename);
#endif

    /* Step 2: Prescan (generic) with fallback to legacy contributor extractor */
    processing_error_init(&step_error, "prescan");
    TLV_PROFILE_START(prescan_profile_stamp);
    result = prescan_and_strip_tokens(sanitized_filename, field_registry,
                                     csv_manager, output_record,
                                     processed_filename, &step_error);
    TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN, prescan_profile_stamp);
    if (result != PROCESSING_SUCCESS) {
        *error_summary = step_error;
        return result;
    }

    /* Legacy compatibility: if no prescan config was provided for contributors, run old path */
    if (processed_filename[0] == '\0') {
        /* Should not happen; ensure we have something to tokenize */
        {
            size_t sl = strlen(sanitized_filename);
            if (sl >= sizeof(processed_filename)) { sl = sizeof(processed_filename) - 1; }
            memcpy(processed_filename, sanitized_filename, sl);
            processed_filename[sl] = '\0';
        }
    }

    /* Step 3: Tokenize filename */
    tf = tokenized_filename_alloc();
    if (!tf) {
        processing_error_set(error_summary, PROCESSING_ERROR_MEMORY_ALLOCATION, NULL, filename,
                             "Failed to allocate tokenized filename structure");
        return PROCESSING_ERROR_MEMORY_ALLOCATION;
    }

    processing_error_init(&step_error, "tokenizer");
    TLV_PROFILE_START(tokenize_profile_stamp);
    result = tokenize_filename(processed_filename, tf, &step_error);
    TLV_PROFILE_END(TLV_PROFILE_SECTION_TOKENIZE, tokenize_profile_stamp);
    if (result != PROCESSING_SUCCESS) {
        *error_summary = step_error;
        tokenized_filename_free(tf);
        return result;
    }

#if PLATFORM_AMIGA
    whdtlv_log_append("Program name: %s", tf->program_name);
    whdtlv_log_append("Tokens to process: %lu", (unsigned long)tf->token_count);
#endif

    /* Generic prescan already handled multi-token fields like contributors; no legacy pre-pass needed */

    /* Step 4: Process each token */
    TLV_PROFILE_START(token_loop_profile_stamp);
    for (uint32_t i = 0; i < tf->token_count; i++) {
        const char *token = tf->tokens[i];
        const char *ampersand_parts[MAX_TOKENS];
        uint32_t ampersand_part_count = 0;
        char ampersand_buffer[MAX_TOKEN_LENGTH];
        /* Step A: prehash state for plain token and &-split parts */
        char lower_token[MAX_TOKEN_LENGTH];
        uint32_t token_raw_hash;
        uint16_t token_prehash_len;
        uint16_t token_prehash_fp;
        uint32_t amp_raw_hash[MAX_TOKENS];
        uint16_t amp_prehash_len[MAX_TOKENS];
        uint16_t amp_prehash_fp[MAX_TOKENS];
        bool version_candidate;
        bool language_candidate;
        bool sps_candidate;
        if (!token || token[0] == '\0') {
            continue; /* consumed by pre-pass or empty */
        }
        bool token_processed = false;
        version_candidate = token_might_be_version(token);
        language_candidate = token_might_be_language(token);
        sps_candidate = token_might_be_sps(token);

        /* Try version parsing first */
        TLV_PROFILE_START(token_checks_profile_stamp);
        if (version_candidate) {
            char clean_version[MAX_VERSION_LENGTH];
            processing_error_init(&step_error, "version_parser");
            if (version_parser_extract(token, clean_version, &step_error) == PROCESSING_SUCCESS) {
#if PLATFORM_AMIGA
                whdtlv_log_append("Found version: %s", clean_version);
#endif
                /* Add version to TLV record */
                if (version_field_id != 0) {
                    tlv_record_add_entry(output_record, version_field_id,
                                       (const uint8_t*)clean_version, strlen(clean_version));
#if PLATFORM_AMIGA
                    whdtlv_log_append("Added version '%s' to TLV record (field_id=0x%02X)", clean_version, version_field_id);
#endif
                }
                token_processed = true;
            }
        }

        /* Try language parsing */
        if (!token_processed && language_candidate) {
            uint16_t language_bitfield;
            processing_error_init(&step_error, "language_parser");
            if (language_parser_parse_token(token, field_registry, csv_manager,
                                           &language_bitfield, &step_error) == PROCESSING_SUCCESS) {
                if (language_bitfield > 0) {
#if PLATFORM_AMIGA
                    whdtlv_log_append("Found language bitfield: 0x%04X", language_bitfield);
#endif
                    /* Add language bitfield to TLV record */
                    if (language_field_id != 0) {
                        uint8_t lang_be[2]; /* no current read path; stored as big-endian uint16 */
                        lang_be[0] = (uint8_t)(language_bitfield >> 8);
                        lang_be[1] = (uint8_t)(language_bitfield & 0xFF);
                        tlv_record_add_entry(output_record, language_field_id,
                                           lang_be, 2);
#if PLATFORM_AMIGA
                        whdtlv_log_append("Added language bitfield 0x%04X to TLV record (field_id=0x%02X)",
                                     language_bitfield, language_field_id);
#endif
                    }
                    token_processed = true;
                }
            }
        }

        /* Try SPS numeric detection (e.g., 4-digit IDs like 1653). Also support multi-valued 'sps' joined by '&'. */
        if (!token_processed && sps_candidate) {
            if (sps_field_id != 0) {
                /* If token contains '&', split into sub-values; else treat as single */
                const char *cursor = token;
                do {
                    char part_buf[16];
                    size_t p = 0;
                    while (*cursor && *cursor != '&' && p < sizeof(part_buf) - 1) {
                        part_buf[p++] = *cursor++;
                    }
                    part_buf[p] = '\0';

                    /* Skip '&' if present */
                    if (*cursor == '&') { cursor++; }

                    /* Check numeric digits-only */
                    bool digits_only = true;
                    size_t plen = strlen(part_buf);
                    if (plen >= 3 && plen <= 6) {
                        for (size_t k = 0; k < plen; k++) {
                            if (!isdigit((unsigned char)part_buf[k])) { digits_only = false; break; }
                        }
                        if (digits_only) {
                            uint32_t sps_id = (uint32_t)atoi(part_buf);
                            uint8_t sps_be[4]; /* no current read path; stored as big-endian uint32 */
                            sps_be[0] = (uint8_t)(sps_id >> 24);
                            sps_be[1] = (uint8_t)(sps_id >> 16);
                            sps_be[2] = (uint8_t)(sps_id >>  8);
                            sps_be[3] = (uint8_t)(sps_id & 0xFF);
                            tlv_record_add_entry(output_record, sps_field_id,
                                                sps_be, 4);
#if PLATFORM_AMIGA
                            whdtlv_log_append("Added SPS ID %lu to TLV record (field_id=0x%02X)", (unsigned long)sps_id, sps_field_id);
#endif
                            token_processed = true; /* at least one part matched */
                        }
                    }
                } while (*cursor != '\0');
            }
        }
        TLV_PROFILE_END(TLV_PROFILE_SECTION_TOKEN_CHECKS, token_checks_profile_stamp);

        /* Try CSV lookups for remaining pack-specific fields */
    if (!token_processed && (pack_matchers != NULL || pack_info->field_list)) {
            if (strchr(token, '&') != NULL) {
                size_t token_length = strlen(token);
                char *ampersand_saveptr = NULL;
                char *ampersand_part;
                uint32_t pi;

                if (token_length >= sizeof(ampersand_buffer)) {
                    token_length = sizeof(ampersand_buffer) - 1;
                }
                memcpy(ampersand_buffer, token, token_length);
                ampersand_buffer[token_length] = '\0';

                ampersand_part = whd_strtok_r(ampersand_buffer, "&", &ampersand_saveptr);
                while (ampersand_part && ampersand_part_count < MAX_TOKENS) {
                    ampersand_parts[ampersand_part_count++] = ampersand_part;
                    ampersand_part = whd_strtok_r(NULL, "&", &ampersand_saveptr);
                }

                /* Step A: lowercase each part in-place and precompute hash/len/fp once.
                 * Parts live in ampersand_buffer which is mutable, so in-place is safe. */
                for (pi = 0; pi < ampersand_part_count; pi++) {
                    uint32_t h = 5381;
                    uint16_t l = 0;
                    char *pw = (char *)ampersand_parts[pi];
                    while (*pw) {
                        unsigned char c  = (unsigned char)*pw;
                        unsigned char lc = (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
                        *pw = (char)lc;
                        h = ((h << 5) + h) + (uint32_t)lc;
                        l++;
                        pw++;
                    }
                    amp_raw_hash[pi]    = h;
                    amp_prehash_len[pi] = l;
                    amp_prehash_fp[pi]  = (uint16_t)(h & 0xFFFFU);
                }
            }

            /* Step A: lowercase + hash the plain token once, before the field loop. */
            token_compute_prehash(token, lower_token, &token_raw_hash,
                                  &token_prehash_len, &token_prehash_fp);

            TLV_PROFILE_START(pack_field_match_profile_stamp);
            uint32_t matcher_total = pack_matchers ? pack_matcher_count : (uint32_t)pack_info->num_fields;
            for (uint32_t j = 0; j < matcher_total; j++) {
                const char *field_name = pack_matchers ? pack_matchers[j].field_name : pack_info->field_list[j];
                const char *csv_name = pack_matchers ? pack_matchers[j].csv_name : field_registry_get_csv_basename(field_registry, field_name);
                uint8_t csv_field_id = pack_matchers ? pack_matchers[j].field_id : field_registry_get_id(field_registry, field_name);
                CSVCache *resolved_cache = pack_matchers ? pack_matchers[j].resolved_cache : NULL;
                bool generic_csv_match_enabled = pack_matchers ? pack_matchers[j].generic_csv_match_enabled : true;
                uint32_t token_id;

                /* Raw filename field eliminated; no need to skip */
                if (!generic_csv_match_enabled || !csv_name || csv_name[0] == '\0') {
                    continue; /* No CSV backing for this field */
                }

                /* If token contains '&', try each already-lowercased sub-part */
                if (ampersand_part_count > 0) {
                    bool any_match = false;
                    for (uint32_t part_index = 0; part_index < ampersand_part_count; part_index++) {
                        const char *part_buf = ampersand_parts[part_index]; /* lowercase (Step A) */

                        if (part_buf && part_buf[0] != '\0') {
                            processing_error_init(&step_error, "csv_token_matcher");
                            if ((resolved_cache != NULL &&
                                 (token_id = csv_cache_lookup_prehashed(resolved_cache, part_buf,
                                                                        amp_prehash_len[part_index],
                                                                        amp_prehash_fp[part_index],
                                                                        amp_raw_hash[part_index])) != 0) ||
                                (resolved_cache == NULL &&
                                 csv_token_matcher_lookup(part_buf, csv_name, field_registry,
                                                       csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS)) {
#if PLATFORM_AMIGA
                                whdtlv_log_append("Found token '%s' in %s.csv (ID=%lu)", part_buf, csv_name, (unsigned long)token_id);
#endif
                                /* Add CSV token ID to TLV record */
                                if (csv_field_id != 0) {
                                    uint8_t token_id_be[4]; /* stored as big-endian uint32 */
                                    token_id_be[0] = (uint8_t)(token_id >> 24);
                                    token_id_be[1] = (uint8_t)(token_id >> 16);
                                    token_id_be[2] = (uint8_t)(token_id >>  8);
                                    token_id_be[3] = (uint8_t)(token_id & 0xFF);
                                    tlv_record_add_entry(output_record, csv_field_id,
                                                       token_id_be, 4);
#if PLATFORM_AMIGA
                                    whdtlv_log_append("Added %s token ID %lu to TLV record (field_id=0x%02X)",
                                                 csv_name, (unsigned long)token_id, csv_field_id);
#endif
                                }
                                any_match = true;
                            }
                        }
                    }

                    if (any_match) {
#if TLV_PROFILE_ENABLE
                        pack_field_record_hit(field_name);
#endif
                        token_processed = true;
                        break;
                    }
                } else {
                    processing_error_init(&step_error, "csv_token_matcher");
                    if ((resolved_cache != NULL &&
                         (token_id = csv_cache_lookup_prehashed(resolved_cache, lower_token,
                                                                token_prehash_len, token_prehash_fp,
                                                                token_raw_hash)) != 0) ||
                        (resolved_cache == NULL &&
                         csv_token_matcher_lookup(token, csv_name, field_registry,
                                               csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS)) {
#if PLATFORM_AMIGA
                        whdtlv_log_append("Found token '%s' in %s.csv (ID=%lu)", token, csv_name, (unsigned long)token_id);
#endif
                        /* Add CSV token ID to TLV record */
                        if (csv_field_id != 0) {
                            uint8_t token_id_be[4]; /* stored as big-endian uint32 */
                            token_id_be[0] = (uint8_t)(token_id >> 24);
                            token_id_be[1] = (uint8_t)(token_id >> 16);
                            token_id_be[2] = (uint8_t)(token_id >>  8);
                            token_id_be[3] = (uint8_t)(token_id & 0xFF);
                            tlv_record_add_entry(output_record, csv_field_id,
                                               token_id_be, 4); /* stored as big-endian uint32 */
#if PLATFORM_AMIGA
                            whdtlv_log_append("Added %s token ID %lu to TLV record (field_id=0x%02X)",
                                         csv_name, (unsigned long)token_id, csv_field_id);
#endif
                        }
#if TLV_PROFILE_ENABLE
                        pack_field_record_hit(field_name);
#endif
                        token_processed = true;
                        break;
                    }
                }
            }
            TLV_PROFILE_END(TLV_PROFILE_SECTION_PACK_FIELD_CSV_MATCH, pack_field_match_profile_stamp);
        }

        /* Add unknown tokens for review */
        if (!token_processed) {
            TLV_PROFILE_START(unknown_token_profile_stamp);
            csv_cache_add_unknown_token_ex(csv_manager, token, filename, pack_info->id);
            TLV_PROFILE_END(TLV_PROFILE_SECTION_UNKNOWN_TOKEN, unknown_token_profile_stamp);
#if PLATFORM_AMIGA
            whdtlv_log_append("Unknown token: %s", token);
#endif
        }
    }
    TLV_PROFILE_END(TLV_PROFILE_SECTION_TOKEN_LOOP, token_loop_profile_stamp);

    tokenized_filename_free(tf);

#if PLATFORM_AMIGA
    whdtlv_log_append("Filename processing completed successfully");
#endif

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Pack Type Management - Removed duplicate implementation */
/* Use whdtlv_load_pack_types() from src/io/pack_types_loader.c instead */

/*------------------------------------------------------------------------*/
/* Pack Type Management - Removed duplicate implementation */
/* Use whdtlv_free_pack_types() from src/io/pack_types_loader.c instead */

/**
 * @brief Get pack type by ID
 */
const PackType *get_pack_type_by_id(const PackType *pack_types, size_t pack_type_count, uint8_t pack_id) {
    if (!pack_types) {
        return NULL;
    }

    for (size_t i = 0; i < pack_type_count; i++) {
        if (pack_types[i].id == pack_id) {
            return &pack_types[i];
        }
    }

    return NULL;
}

/*------------------------------------------------------------------------*/

/* End of Text */
