/* error_handling.h - TLV Filename Processing Error System
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Comprehensive error handling system for TLV filename processing with
 * detailed context tracking and module-specific error reporting.
 *
 * Key Features:
 * - ProcessingResult enum for standardized error codes
 * - ProcessingError structure for detailed error context
 * - Module-specific error tracking with token and filename context
 * - String truncation with proper null termination
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#ifndef TLV_FILENAME_ERROR_HANDLING_H
#define TLV_FILENAME_ERROR_HANDLING_H

#pragma once

#include <platform.h>

/*------------------------------------------------------------------------*/
/* Error Codes */

/**
 * @brief Result codes for TLV filename processing operations
 */
typedef enum {
    PROCESSING_SUCCESS = 0,                     /* Operation completed successfully */
    PROCESSING_WARNING_PARTIAL,                 /* Operation partially successful with warnings */
    PROCESSING_ERROR_INVALID_INPUT,             /* Invalid input parameters */
    PROCESSING_ERROR_MEMORY_ALLOCATION,         /* Memory allocation failed */
    PROCESSING_ERROR_CSV_LOOKUP_FAILED,         /* CSV file access or lookup failed */
    PROCESSING_ERROR_TOKEN_NOT_FOUND,           /* Token not found in CSV or processing */
    PROCESSING_ERROR_INVALID_FORMAT,            /* Invalid format or data structure */
    PROCESSING_ERROR_TLV_RECORD_ADDITION        /* Failed to add entry to TLV record */
} ProcessingResult;

/*------------------------------------------------------------------------*/
/* Error Context */

/**
 * @brief Comprehensive error context for TLV processing operations
 *
 * Provides detailed context about processing failures including the specific
 * token that failed, source filename, and human-readable error message.
 */
typedef struct {
    char error_message[256];        /* Human-readable error description */
    char failed_token[64];          /* Token that caused the failure */
    char source_filename[128];      /* Original filename being processed */
    ProcessingResult error_code;    /* Specific error code */
    const char *module_name;        /* Which module reported the error */
} ProcessingError;

/*------------------------------------------------------------------------*/
/* Error Management Functions */

/**
 * @brief Initialize error context structure
 * @param error Error structure to initialize
 * @param module_name Name of module that will set errors
 */
void processing_error_init(ProcessingError *error, const char *module_name);

/**
 * @brief Set error context with formatted message
 * @param error Error structure to populate
 * @param code Error code
 * @param token Failed token (can be NULL)
 * @param filename Source filename (can be NULL)
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void processing_error_set(ProcessingError *error, ProcessingResult code,
                         const char *token, const char *filename,
                         const char *format, ...);

/**
 * @brief Check if error structure indicates an error condition
 * @param error Error structure to check
 * @return true if error is set, false if success
 */
bool processing_error_is_set(const ProcessingError *error);

/**
 * @brief Clear error structure back to success state
 * @param error Error structure to clear
 */
void processing_error_clear(ProcessingError *error);

/*------------------------------------------------------------------------*/

#endif /* TLV_FILENAME_ERROR_HANDLING_H */

/* End of Text */
