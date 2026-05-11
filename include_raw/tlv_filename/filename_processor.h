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
#include <stdio.h>
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

typedef struct {
    const char *field_name;
    const char *csv_name;
    uint8_t field_id;
    CSVCache *resolved_cache;
    bool generic_csv_match_enabled;
} PackFieldMatcher;

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
                                                 const PackFieldMatcher *pack_matchers,
                                                 uint32_t pack_matcher_count,
                                                 struct TLV_Record *output_record,
                                                 ProcessingError *error_summary);

/*------------------------------------------------------------------------*/
/* Individual Processing Steps */

/*------------------------------------------------------------------------*/
/* Specialized Parsers */

/*------------------------------------------------------------------------*/
/* Pack Type Management */

/* load_pack_types / free_pack_types: canonical declarations are in
 * <io/pack_types_loader.h>, which is included above via the PackType
 * typedef. Do not redeclare them here. */

/*------------------------------------------------------------------------*/
/* Profiling Output */

/**
 * @brief Print per-field pack match hit counts (profiling builds only)
 * @param stream Output stream to write to
 */
void filename_processor_print_pack_field_stats(FILE *stream);

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_FILENAME_PROCESSOR_H */

/* End of Text */
