/* filename_processor.h - TLV Filename Processing Orchestrator
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Main orchestration engine for processing WHDLoad package filenames into
 * structured TLV metadata using dynamic field registry and CSV cache system.
 *
 * Key Features:
 * - Error-aware orchestration with comprehensive error handling
 * - Multi-step processing pipeline (sanitize, extract, tokenize, match)
 * - Integration with dynamic field registry and CSV cache
 * - Pack-type-driven processing based on pack_types.ini configuration
 * - Production-ready error reporting and logging
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#ifndef TLV_FILENAME_FILENAME_PROCESSOR_H
#define TLV_FILENAME_FILENAME_PROCESSOR_H

#pragma once

#include <platform.h>
#include <tlv_filename/error_handling.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/csv_cache.h>

/*------------------------------------------------------------------------*/
/* Forward Declarations */

/* TLV Record structure forward declaration (full type in tlv_builder.h) */
struct TLV_Record;

/*------------------------------------------------------------------------*/
/* Pack Type Configuration */

/* Use canonical PackType definition from pack_types_loader.h */
#include <io/pack_types_loader.h>

/*------------------------------------------------------------------------*/
/* Core Processing Functions */

/**
 * @brief Main orchestrator function with comprehensive error handling
 *
 * Coordinates the entire filename processing pipeline from raw filename
 * to populated TLV record using dynamic field registry and CSV cache.
 *
 * @param filename Input filename to process
 * @param pack_info Pack type configuration
 * @param field_registry Dynamic field registry for ID resolution
 * @param csv_manager Pre-loaded CSV cache manager
 * @param output_record TLV record to populate
 * @param error_summary Output: comprehensive error summary
 * @return ProcessingResult indicating overall success or failure
 */
ProcessingResult tlv_process_filename_orchestrator(const char *filename,
                                                 const PackType *pack_info,
                                                 const FieldRegistry *field_registry,
                                                 GlobalCSVManager *csv_manager,
                                                 struct TLV_Record *output_record,
                                                 ProcessingError *error_summary);

/*------------------------------------------------------------------------*/
/* Individual Processing Steps */

/**
 * @brief Sanitize filename (remove extension, normalize format)
 * @param input Raw filename with extension
 * @param output Sanitized filename without extension
 * @param error Error context if operation fails
 * @return ProcessingResult indicating success or failure
 */
ProcessingResult filename_sanitizer_process(const char *input,
                                          char *output,
                                          ProcessingError *error);

/**
 * @brief Extract multi-token contributors from filename
 * @param filename Input filename to process
 * @param field_registry Dynamic field registry for field ID resolution
 * @param csv_manager CSV cache for contributor lookups
 * @param output_record TLV record to store found contributors
 * @param processed_filename Output filename with contributors removed
 * @param error Error context if extraction fails
 * @return ProcessingResult indicating success or failure
 */
ProcessingResult contributor_extractor_process(const char *filename,
                                             const FieldRegistry *field_registry,
                                             GlobalCSVManager *csv_manager,
                                             struct TLV_Record *output_record,
                                             char *processed_filename,
                                             ProcessingError *error);

/**
 * @brief Match token against specific CSV using dynamic field registry
 * @param token Token to match
 * @param csv_name CSV cache name to search
 * @param field_registry Dynamic field registry for field ID resolution
 * @param csv_manager Global CSV cache manager
 * @param token_id Output: ID if found
 * @param error Error context if match fails
 * @return ProcessingResult indicating match result
 */
ProcessingResult csv_token_matcher_lookup(const char *token,
                                        const char *csv_name,
                                        const FieldRegistry *field_registry,
                                        GlobalCSVManager *csv_manager,
                                        uint32_t *token_id,
                                        ProcessingError *error);

/**
 * @brief Find which CSV contains the given token using field registry
 * @param token Token to search for
 * @param field_registry Dynamic field registry for available CSVs
 * @param csv_manager Global CSV cache manager
 * @param csv_source Output: CSV name if found
 * @param token_id Output: ID if found
 * @param error Error context if search fails
 * @return ProcessingResult indicating search result
 */
ProcessingResult csv_token_matcher_find_source(const char *token,
                                              const FieldRegistry *field_registry,
                                              GlobalCSVManager *csv_manager,
                                              const char **csv_source,
                                              uint32_t *token_id,
                                              ProcessingError *error);

/*------------------------------------------------------------------------*/
/* Specialized Parsers */

/**
 * @brief Parse language token into bitfield using field registry
 * @param language_token Token to parse (e.g., "EnFrDe")
 * @param field_registry Dynamic field registry for field ID resolution
 * @param csv_manager CSV cache for language validation
 * @param language_bitfield Output: constructed bitfield
 * @param error Error context if parsing fails
 * @return ProcessingResult indicating parsing result
 */
ProcessingResult language_parser_parse_token(const char *language_token,
                                            const FieldRegistry *field_registry,
                                            GlobalCSVManager *csv_manager,
                                            uint16_t *language_bitfield,
                                            ProcessingError *error);

/**
 * @brief Parse version token and extract clean version
 * @param version_token Token potentially containing version
 * @param clean_version Output: cleaned version string
 * @param error Error context if parsing fails
 * @return ProcessingResult indicating parsing result
 */
ProcessingResult version_parser_extract(const char *version_token,
                                       char *clean_version,
                                       ProcessingError *error);

/**
 * @brief Detect if token contains version pattern
 * @param token Token to check
 * @param error Error context if detection fails
 * @return ProcessingResult indicating detection result
 */
ProcessingResult version_parser_detect_pattern(const char *token,
                                              ProcessingError *error);

/*------------------------------------------------------------------------*/
/* Pack Type Management */

/**
 * @brief Load pack type configurations from pack_types.ini
 * @param pack_types_ini_path Path to pack_types.ini file
 * @param pack_type_count Output: number of pack types loaded
 * @return Array of PackType structures, NULL on error
 */
PackType *load_pack_types(const char *pack_types_ini_path, size_t *pack_type_count);

/**
 * @brief Free array of pack type configurations
 * @param pack_types Array of PackType structures to free
 * @param pack_type_count Number of pack types in array
 */
void free_pack_types(PackType *pack_types, size_t pack_type_count);

/**
 * @brief Get pack type by ID
 * @param pack_types Array of PackType structures
 * @param pack_type_count Number of pack types in array
 * @param pack_id Pack type ID to find
 * @return Pointer to PackType if found, NULL if not found
 */
const PackType *get_pack_type_by_id(const PackType *pack_types, size_t pack_type_count, uint8_t pack_id);

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_FILENAME_PROCESSOR_H */

/* End of Text */
