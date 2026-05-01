/* pack_types_loader.h — Pack types configuration loader for WHD Downloader 2
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * This module loads pack type definitions from pack_types.ini and validates
 * them strictly according to Amiga filename constraints and metadata system
 * requirements.
 *
 * Author: Kerry Thompson
 * Created: 2025-07-30
 * Updated: auto-managed via Git
 */

#ifndef PACK_TYPES_LOADER_H
#define PACK_TYPES_LOADER_H

#include <platform.h>
#include <stdint.h>
#include <stddef.h>

/*------------------------------------------------------------------------*/
/* PackType Structure                                                     */
/*------------------------------------------------------------------------*/

/**
 * @brief Pack type definition loaded from INI file
 *
 * Contains all metadata about a WHDLoad pack type including display
 * information, search parameters, and associated metadata fields.
 */
typedef struct {
    uint8_t    id;              /* Pack type ID (1-255) */
    char      *display_name;    /* Display name (1-64 chars) */
    char      *abbrev;          /* Abbreviation (1-16 chars) */
    char      *search_snippet;  /* URL-encoded search query (1-128 chars) */
    char      *dat_name;        /* DAT filename prefix (max 4 chars, Amiga-compatible) */
    char     **field_list;      /* NULL-terminated field list */
    size_t     num_fields;      /* Number of fields in field_list */
} PackType;

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/

/**
 * @brief Load pack types from INI file
 *
 * Loads and validates pack type definitions from the specified INI file.
 * Performs strict validation on all fields according to Amiga filename
 * constraints and metadata system requirements.
 *
 * On validation error, prints error message with line number and exits
 * with non-zero status.
 *
 * @param ini_path Path to pack_types.ini file
 * @param out_count Pointer to receive number of loaded pack types
 * @return Dynamically allocated array of PackType structures, or NULL on error
 */
PackType *load_pack_types(const char *ini_path, size_t *out_count);

/**
 * @brief Free pack types array
 *
 * Frees all memory allocated for the pack types array including all
 * string fields and the array itself.
 *
 * @param packs Pack types array to free
 * @param count Number of pack types in array
 */
void free_pack_types(PackType *packs, size_t count);

/*------------------------------------------------------------------------*/
/* Validation Helper                                                      */
/*------------------------------------------------------------------------*/

/**
 * @brief Split string on delimiter with validation
 *
 * Splits a string on the specified delimiter and validates that exactly
 * the expected number of parts are returned. Useful for parsing INI
 * values and CSV data.
 *
 * @param str String to split
 * @param delimiter Delimiter character
 * @param expected_parts Expected number of parts after splitting
 * @param out_parts Pointer to receive allocated array of string parts
 * @return true if successful with correct part count, false otherwise
 */
bool validate_and_split(const char *str, char delimiter, int expected_parts, char ***out_parts);

/**
 * @brief Trim whitespace from string
 *
 * Creates a new string with leading and trailing whitespace removed.
 * Preserves the original case of non-whitespace characters.
 *
 * @param src Source string to trim
 * @return Newly allocated trimmed string, or empty string if all whitespace
 */
char *strtrim(const char *src);

#endif /* PACK_TYPES_LOADER_H */

/* End of Text */
