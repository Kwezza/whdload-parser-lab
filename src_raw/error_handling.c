/* error_handling.c - TLV Filename Processing Error System Implementation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implementation of comprehensive error handling system for TLV filename
 * processing with detailed context tracking and module-specific error reporting.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#include <platform.h>
#include <tlv_filename/error_handling.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*------------------------------------------------------------------------*/

/**
 * @brief Initialize error context structure
 */
void processing_error_init(ProcessingError *error, const char *module_name)
{
    if (!error) {
        return;
    }

    /* Clear all fields */
    memset(error, 0, sizeof(ProcessingError));

    /* Set module name and success state */
    error->module_name = module_name;
    error->error_code = PROCESSING_SUCCESS;
} /* processing_error_init */

/*------------------------------------------------------------------------*/

/**
 * @brief Set error context with formatted message
 */
void processing_error_set(ProcessingError *error, ProcessingResult code,
                         const char *token, const char *filename,
                         const char *format, ...)
{
    va_list args;

    if (!error || !format) {
        return;
    }

    /* Set error code */
    error->error_code = code;

    /* Copy token (with truncation) */
    if (token) {
        strncpy(error->failed_token, token, sizeof(error->failed_token) - 1);
        error->failed_token[sizeof(error->failed_token) - 1] = '\0';
    } else {
        error->failed_token[0] = '\0';
    }

    /* Copy filename (with truncation) */
    if (filename) {
        strncpy(error->source_filename, filename, sizeof(error->source_filename) - 1);
        error->source_filename[sizeof(error->source_filename) - 1] = '\0';
    } else {
        error->source_filename[0] = '\0';
    }

    /* Format error message (with truncation) */
    va_start(args, format);
    vsnprintf(error->error_message, sizeof(error->error_message), format, args);
    va_end(args);

    /* Ensure null termination */
    error->error_message[sizeof(error->error_message) - 1] = '\0';
} /* processing_error_set */

/*------------------------------------------------------------------------*/

/**
 * @brief Check if error structure indicates an error condition
 */
bool processing_error_is_set(const ProcessingError *error)
{
    if (!error) {
        return false;
    }

    return (error->error_code != PROCESSING_SUCCESS);
} /* processing_error_is_set */

/*------------------------------------------------------------------------*/

/**
 * @brief Clear error structure back to success state
 */
void processing_error_clear(ProcessingError *error)
{
    if (!error) {
        return;
    }

    error->error_code = PROCESSING_SUCCESS;
    error->error_message[0] = '\0';
    error->failed_token[0] = '\0';
    error->source_filename[0] = '\0';
    /* Note: module_name is preserved */
} /* processing_error_clear */

/*------------------------------------------------------------------------*/

/* End of Text */
