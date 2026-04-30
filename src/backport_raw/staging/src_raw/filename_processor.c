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

#include <platform.h>
#include <tlv_filename/filename_processor.h>
#include <tlv_filename/error_handling.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/csv_cache.h>
#include <io/writeLog.h>

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <platform/platform_string.h>

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

/*------------------------------------------------------------------------*/
/* Filename Sanitization */

/**
 * @brief Sanitize filename (remove extension, normalize format)
 */
ProcessingResult filename_sanitizer_process(const char *input,
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
ProcessingResult version_parser_extract(const char *version_token,
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
 * @brief Parse language token into bitfield using field registry
 */
ProcessingResult language_parser_parse_token(const char *language_token,
                                            const FieldRegistry *field_registry,
                                            GlobalCSVManager *csv_manager,
                                            uint16_t *language_bitfield,
                                            ProcessingError *error) {
    if (!language_token || !field_registry || !csv_manager || !language_bitfield) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT, NULL, NULL,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    *language_bitfield = 0;

    /* Check if this looks like a language token (2-char combinations) */
    size_t len = strlen(language_token);
    if (len < 2 || len % 2 != 0 || len > MAX_LANGUAGE_CHARS) {
        processing_error_set(error, PROCESSING_ERROR_INVALID_FORMAT, language_token, NULL,
                             "Invalid language token format");
        return PROCESSING_ERROR_INVALID_FORMAT;
    }

    /* Parse each 2-character language code */
    for (size_t i = 0; i < len; i += 2) {
        char lang_code[3] = {language_token[i], language_token[i+1], '\0'};

        /* Look up language code in CSV */
        uint32_t lang_id = csv_cache_lookup(csv_manager, "Language", lang_code);
        if (lang_id > 0 && lang_id <= 16) {  /* Assuming max 16 languages for bitfield */
            *language_bitfield |= (1 << (lang_id - 1));
        }
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
                tlv_record_add_entry(output_record, contributor_field_id,
                                   (const uint8_t*)&contributor_id, sizeof(contributor_id));
#if PLATFORM_AMIGA
                append_to_log("Added contributor '%s' ID %lu to TLV record (field_id=0x%02X)",
                             contributor_phrase, (unsigned long)contributor_id, contributor_field_id);
#endif
            }
#if PLATFORM_AMIGA
            append_to_log("Found contributor: %s (ID=%lu)", contributor_phrase, (unsigned long)contributor_id);
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

    /* Targeted debug: list prescan fields for known sample names */
    if (filename && (strstr(filename, "Kernal_Version") != NULL ||
                     strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL)) {
    append_to_log("PRESCAN ACTIVE: %lu field(s)", (unsigned long)cfg_count);
        for (uint32_t dc = 0; dc < cfg_count; dc++) {
            append_to_log("  field=%s csv=%s order=%lu multi=%u remove=%u multival=%u",
                          cfgs[dc].field_name ? cfgs[dc].field_name : "?",
                          cfgs[dc].csv_base ? cfgs[dc].csv_base : "",
                          (unsigned long)cfgs[dc].order,
                          cfgs[dc].multi_token ? 1u : 0u,
                          cfgs[dc].remove_from_filename ? 1u : 0u,
                          cfgs[dc].allow_multiple ? 1u : 0u);
        }
    }

    /* For each configured field in order, attempt matches */
    for (uint32_t c = 0; c < cfg_count; c++) {
        const FieldPrescanConfig *cfg = &cfgs[c];
        if (!cfg->enabled || !cfg->csv_base || cfg->csv_base[0] == '\0') {
            continue;
        }

        /* Tokenize a transient copy (do not modify processed_filename directly for joins) */
        char tmp[MAX_FILENAME_LENGTH];
        {
            size_t tlen = strlen(processed_filename);
            if (tlen >= sizeof(tmp)) { tlen = sizeof(tmp) - 1; }
            memcpy(tmp, processed_filename, tlen);
            tmp[tlen] = '\0';
        }

    /* Split by underscores */
        char *parts[MAX_TOKENS];
        uint32_t pc = 0;
        char *saveptr = NULL;
        char *tok = whd_strtok_r(tmp, "_", &saveptr);
        while (tok && pc < MAX_TOKENS) {
            parts[pc++] = tok;
            tok = whd_strtok_r(NULL, "_", &saveptr);
        }

    /* Window sizes: prefer longer spans first; if multi_token, try full length down to 1 */
    uint32_t max_window = cfg->multi_token ? pc : 1U;
    /* any_found reserved for future logging; avoid unused on host */
#if PLATFORM_AMIGA
    bool any_found = false;
#endif

    for (uint32_t window = max_window; window >= 1; window--) {
            if (pc < window) { continue; }
            for (uint32_t i = 0; i + window <= pc; i++) {
                /* Build joined token */
                char joined[MAX_TOKEN_LENGTH];
                joined[0] = '\0';
                for (uint32_t k = 0; k < window; k++) {
                    if (k > 0) { strcat(joined, "_"); }
                    if (strlen(joined) + strlen(parts[i + k]) >= sizeof(joined)) {
                        joined[0] = '\0';
                        break;
                    }
                    strcat(joined, parts[i + k]);
                }
                if (joined[0] == '\0') { continue; }

                if (filename && (strstr(filename, "Kernal_Version") != NULL ||
                                 strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL)) {
                    append_to_log("PRESCAN TRY: field=%s csv=%s window=%lu token='%s'",
                                  cfg->field_name ? cfg->field_name : "?",
                                  cfg->csv_base ? cfg->csv_base : "",
                                  (unsigned long)window,
                                  joined);
                }

        /* Lookup in CSV (column 1 as token) */
                uint32_t id = csv_cache_lookup(csv_manager, cfg->csv_base, joined);
                if (id > 0) {
                    /* Add TLV entry storing the ID */
                    if (cfg->field_id != 0) {
                        tlv_record_add_entry(output_record, cfg->field_id,
                                             (const uint8_t *)&id, sizeof(id));
                    }
                    /* Targeted, low-noise debug for known sample names */
                    if (filename && (strstr(filename, "Kernal_Version") != NULL ||
                                     strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL)) {
                        append_to_log("PRESCAN MATCH: field=%s token='%s' id=%lu", cfg->field_name ? cfg->field_name : "?", joined, (unsigned long)id);
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
                        /* Remove the span from processed_filename by reconstructing string without that window */
                        char rebuild[MAX_FILENAME_LENGTH];
                        rebuild[0] = '\0';
                        for (uint32_t p = 0; p < pc; p++) {
                            if (p >= i && p < i + window) { continue; }
                            if (rebuild[0] != '\0') { strcat(rebuild, "_"); }
                            if (strlen(rebuild) + strlen(parts[p]) < sizeof(rebuild)) {
                                strcat(rebuild, parts[p]);
                            }
                        }
                        /* Re-attach possible program name prefix if present in original; handled later by tokenizer */
                        {
                            size_t rlen = strlen(rebuild);
                            if (rlen >= (size_t)MAX_FILENAME_LENGTH) { rlen = (size_t)MAX_FILENAME_LENGTH - 1; }
                            memcpy(processed_filename, rebuild, rlen);
                            processed_filename[rlen] = '\0';
                        }
                        if (filename && (strstr(filename, "Kernal_Version") != NULL ||
                                         strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL)) {
                            append_to_log("PRESCAN STRIP: field=%s result='%s'", cfg->field_name ? cfg->field_name : "?", processed_filename);
                        }
                        /* Update token list for subsequent windows by re-tokenizing */
                        {
                            size_t t2 = strlen(processed_filename);
                            if (t2 >= sizeof(tmp)) { t2 = sizeof(tmp) - 1; }
                            memcpy(tmp, processed_filename, t2);
                            tmp[t2] = '\0';
                        }
                        pc = 0; saveptr = NULL;
                        tok = whd_strtok_r(tmp, "_", &saveptr);
                        while (tok && pc < MAX_TOKENS) { parts[pc++] = tok; tok = whd_strtok_r(NULL, "_", &saveptr); }
                        /* Restart window search from beginning for this field to catch further occurrences */
            i = 0; /* will be reset by full field rescan */
            /* To simplify, restart this field's scan entirely */
                        goto restart_field_scan;
                    }
                }
            }
        }
restart_field_scan:
    /* any_found reserved for future logging */
    }

    return PROCESSING_SUCCESS;
}
/* CSV Token Matching */

/**
 * @brief Match token against specific CSV using dynamic field registry
 */
ProcessingResult csv_token_matcher_lookup(const char *token,
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
                                                 TLV_Record *output_record,
                                                 ProcessingError *error_summary) {
    ProcessingError step_error;
    char sanitized_filename[MAX_FILENAME_LENGTH];
    char processed_filename[MAX_FILENAME_LENGTH];
    TokenizedFilename *tf = NULL;
    ProcessingResult result;

    /* Initialize error context */
    processing_error_init(error_summary, "tlv_orchestrator");

    if (!filename || !pack_info || !field_registry || !csv_manager) {
        processing_error_set(error_summary, PROCESSING_ERROR_INVALID_INPUT, NULL, filename,
                             "Required parameter is NULL");
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Step 0: raw filename externalized to sidecar; we now emit display_name (prettified) field
     * early to serve as a record delimiter. For now we map sanitized filename directly; future
     * enhancement can integrate prettify_title() for nicer spacing/casing. */

    /* Step 1: Sanitize filename */
    processing_error_init(&step_error, "filename_sanitizer");
    result = filename_sanitizer_process(filename, sanitized_filename, &step_error);
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
    append_to_log("Processing filename: %s", sanitized_filename);
#endif

    /* Step 2: Prescan (generic) with fallback to legacy contributor extractor */
    processing_error_init(&step_error, "prescan");
    result = prescan_and_strip_tokens(sanitized_filename, field_registry,
                                     csv_manager, output_record,
                                     processed_filename, &step_error);
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
    result = tokenize_filename(processed_filename, tf, &step_error);
    if (result != PROCESSING_SUCCESS) {
        *error_summary = step_error;
        tokenized_filename_free(tf);
        return result;
    }

#if PLATFORM_AMIGA
    append_to_log("Program name: %s", tf->program_name);
    append_to_log("Tokens to process: %lu", (unsigned long)tf->token_count);
#endif

    /* Generic prescan already handled multi-token fields like contributors; no legacy pre-pass needed */

    /* Step 4: Process each token */
    for (uint32_t i = 0; i < tf->token_count; i++) {
        const char *token = tf->tokens[i];
        if (!token || token[0] == '\0') {
            continue; /* consumed by pre-pass or empty */
        }
        bool token_processed = false;

        /* Try version parsing first */
        processing_error_init(&step_error, "version_parser");
        if (version_parser_detect_pattern(token, &step_error) == PROCESSING_SUCCESS) {
            char clean_version[MAX_VERSION_LENGTH];
            if (version_parser_extract(token, clean_version, &step_error) == PROCESSING_SUCCESS) {
#if PLATFORM_AMIGA
                append_to_log("Found version: %s", clean_version);
#endif
                /* Add version to TLV record */
                uint8_t version_field_id = field_registry_get_id(field_registry, "version");
                if (version_field_id != 0) {
                    tlv_record_add_entry(output_record, version_field_id,
                                       (const uint8_t*)clean_version, strlen(clean_version));
#if PLATFORM_AMIGA
                    append_to_log("Added version '%s' to TLV record (field_id=0x%02X)", clean_version, version_field_id);
#endif
                }
                token_processed = true;
            }
        }

        /* Try language parsing */
        if (!token_processed) {
            processing_error_init(&step_error, "language_parser");
            uint16_t language_bitfield;
            if (language_parser_parse_token(token, field_registry, csv_manager,
                                           &language_bitfield, &step_error) == PROCESSING_SUCCESS) {
                if (language_bitfield > 0) {
#if PLATFORM_AMIGA
                    append_to_log("Found language bitfield: 0x%04X", language_bitfield);
#endif
                    /* Add language bitfield to TLV record */
                    uint8_t language_field_id = field_registry_get_id(field_registry, "language");
                    if (language_field_id != 0) {
                        tlv_record_add_entry(output_record, language_field_id,
                                           (const uint8_t*)&language_bitfield, sizeof(language_bitfield));
#if PLATFORM_AMIGA
                        append_to_log("Added language bitfield 0x%04X to TLV record (field_id=0x%02X)",
                                     language_bitfield, language_field_id);
#endif
                    }
                    token_processed = true;
                }
            }
        }

        /* Try SPS numeric detection (e.g., 4-digit IDs like 1653). Also support multi-valued 'sps' joined by '&'. */
        if (!token_processed) {
            uint8_t sps_field_id = field_registry_get_id(field_registry, "sps");
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
                            tlv_record_add_entry(output_record, sps_field_id,
                                                (const uint8_t*)&sps_id, sizeof(sps_id));
#if PLATFORM_AMIGA
                            append_to_log("Added SPS ID %lu to TLV record (field_id=0x%02X)", (unsigned long)sps_id, sps_field_id);
#endif
                            token_processed = true; /* at least one part matched */
                        }
                    }
                } while (*cursor != '\0');
            }
        }

        /* Try CSV lookups for remaining pack-specific fields */
    if (!token_processed && pack_info->field_list) {
            for (uint32_t j = 0; j < pack_info->num_fields; j++) {
                const char *field_name = pack_info->field_list[j];
                const char *csv_name = get_csv_filename_for_field(field_registry, field_name);
                uint32_t token_id;

                /* Raw filename field eliminated; no need to skip */
                if (!csv_name || csv_name[0] == '\0') {
                    continue; /* No CSV backing for this field */
                }

                /* If token contains '&', split into parts and attempt each; robust to INI flag omissions */
                if (strchr(token, '&') != NULL) {
                    const char *cursor = token;
                    bool any_match = false;
                    do {
                        char part_buf[MAX_TOKEN_LENGTH];
                        size_t p = 0;
                        while (*cursor && *cursor != '&' && p < sizeof(part_buf) - 1) {
                            part_buf[p++] = *cursor++;
                        }
                        part_buf[p] = '\0';
                        if (*cursor == '&') { cursor++; }

                        if (part_buf[0] != '\0') {
                            processing_error_init(&step_error, "csv_token_matcher");
                            if (csv_token_matcher_lookup(part_buf, csv_name, field_registry,
                                                       csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS) {
#if PLATFORM_AMIGA
                                append_to_log("Found token '%s' in %s.csv (ID=%lu)", part_buf, csv_name, (unsigned long)token_id);
#endif
                                /* Add CSV token ID to TLV record */
                                uint8_t csv_field_id = field_registry_get_id(field_registry, field_name);
                                if (csv_field_id != 0) {
                                    tlv_record_add_entry(output_record, csv_field_id,
                                                       (const uint8_t*)&token_id, sizeof(token_id));
#if PLATFORM_AMIGA
                                    append_to_log("Added %s token ID %lu to TLV record (field_id=0x%02X)",
                                                 csv_name, (unsigned long)token_id, csv_field_id);
#endif
                                }
                                any_match = true;
                            }
                        }
                    } while (*cursor != '\0');

                    if (any_match) { token_processed = true; break; }
                } else {
                    processing_error_init(&step_error, "csv_token_matcher");
                    if (csv_token_matcher_lookup(token, csv_name, field_registry,
                                               csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS) {
#if PLATFORM_AMIGA
                        append_to_log("Found token '%s' in %s.csv (ID=%lu)", token, csv_name, (unsigned long)token_id);
#endif
                        /* Add CSV token ID to TLV record */
                        uint8_t csv_field_id = field_registry_get_id(field_registry, field_name);
                        if (csv_field_id != 0) {
                            tlv_record_add_entry(output_record, csv_field_id,
                                               (const uint8_t*)&token_id, sizeof(token_id));
#if PLATFORM_AMIGA
                            append_to_log("Added %s token ID %lu to TLV record (field_id=0x%02X)",
                                         csv_name, (unsigned long)token_id, csv_field_id);
#endif
                        }
                        token_processed = true;
                        break;
                    }
                }
            }
        }

        /* Add unknown tokens for review */
        if (!token_processed) {
            csv_cache_add_unknown_token_ex(csv_manager, token, filename, pack_info->id);
#if PLATFORM_AMIGA
            append_to_log("Unknown token: %s", token);
#endif
        }
    }

    tokenized_filename_free(tf);

#if PLATFORM_AMIGA
    append_to_log("Filename processing completed successfully");
#endif

    return PROCESSING_SUCCESS;
}

/*------------------------------------------------------------------------*/
/* Pack Type Management - Removed duplicate implementation */
/* Use load_pack_types() from src/io/pack_types_loader.c instead */

/*------------------------------------------------------------------------*/
/* Pack Type Management - Removed duplicate implementation */
/* Use free_pack_types() from src/io/pack_types_loader.c instead */

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
