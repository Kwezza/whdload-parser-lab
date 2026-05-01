/* pack_types_loader.c — Pack types configuration loader implementation
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

#include <platform.h>
#include <platform/platform_io.h>
#include <io/pack_types_loader.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/*------------------------------------------------------------------------*/
/* Constants                                                              */
/*------------------------------------------------------------------------*/

#define MAX_LINE_LENGTH 512
#define MAX_DISPLAY_NAME_LEN 64
#define MAX_ABBREV_LEN 16
#define MAX_SEARCH_SNIPPET_LEN 128
#define MAX_DAT_NAME_LEN 4   /* Amiga max filename 15 - hyphen(1) - date(10) = 4 chars max */
#define MAX_FIELD_COUNT 16  /* Increased to accommodate filename field */
#define MAX_PACK_TYPES 32

/*------------------------------------------------------------------------*/
/* Validation Functions                                                   */
/*------------------------------------------------------------------------*/

/**
 * @brief Validate display name field
 *
 * Checks that display name is 1-64 characters and contains only
 * printable ASCII characters.
 *
 * @param display_name Display name to validate
 * @return true if valid, false otherwise
 */
static bool validate_display_name(const char *display_name)
{
    size_t len;
    size_t i;

    if (!display_name) {
        return false;
    } /* if */

    len = strlen(display_name);
    if (len == 0 || len > MAX_DISPLAY_NAME_LEN) {
        return false;
    } /* if */

    /* Check for printable ASCII characters only */
    for (i = 0; i < len; i++) {
        if (display_name[i] < 32 || display_name[i] > 126) {
            return false;
        } /* if */
    } /* for */

    return true;
} /* validate_display_name */

/*------------------------------------------------------------------------*/

/**
 * @brief Validate abbreviation field
 *
 * Checks that abbreviation is 1-16 characters and contains only
 * printable ASCII characters.
 *
 * @param abbrev Abbreviation to validate
 * @return true if valid, false otherwise
 */
static bool validate_abbrev(const char *abbrev)
{
    size_t len;
    size_t i;

    if (!abbrev) {
        return false;
    } /* if */

    len = strlen(abbrev);
    if (len == 0 || len > MAX_ABBREV_LEN) {
        return false;
    } /* if */

    /* Check for printable ASCII characters only */
    for (i = 0; i < len; i++) {
        if (abbrev[i] < 32 || abbrev[i] > 126) {
            return false;
        } /* if */
    } /* for */

    return true;
} /* validate_abbrev */

/*------------------------------------------------------------------------*/

/**
 * @brief Validate search snippet field
 *
 * Checks that search snippet is 1-128 characters and contains only
 * URL-safe characters.
 *
 * @param search_snippet Search snippet to validate
 * @return true if valid, false otherwise
 */
static bool validate_search_snippet(const char *search_snippet)
{
    size_t len;
    size_t i;
    char c;

    if (!search_snippet) {
        return false;
    } /* if */

    len = strlen(search_snippet);
    if (len == 0 || len > MAX_SEARCH_SNIPPET_LEN) {
        return false;
    } /* if */

    /* Check for URL-safe characters */
    for (i = 0; i < len; i++) {
        c = search_snippet[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '%' || c == '(' || c == ')' || c == '-' || c == '_' ||
              c == '.' || c == '~' || c == ':' || c == '/' || c == '?' ||
              c == '#' || c == '[' || c == ']' || c == '@' || c == '!' ||
              c == '$' || c == '&' || c == '\'' || c == '*' || c == '+' ||
              c == ',' || c == ';' || c == '=')) {
            return false;
        } /* if */
    } /* for */

    return true;
} /* validate_search_snippet */

/*------------------------------------------------------------------------*/

/**
 * @brief Validate DAT name field
 *
 * Checks that DAT name is alphanumeric + underscores and satisfies
 * Amiga filename length constraint. Final filename format will be:
 * "DatName-YYYY-MM-DD" which must be ≤ 15 characters total.
 *
 * @param dat_name DAT name to validate
 * @return true if valid, false otherwise
 */
static bool validate_dat_name(const char *dat_name)
{
    size_t len;
    size_t i;
    char c;

    if (!dat_name) {
        return false;
    } /* if */

    len = strlen(dat_name);
    if (len == 0 || len > MAX_DAT_NAME_LEN) {
        return false;
    } /* if */

    /* Check Amiga filename constraint: DatName + hyphen(1) + date(10) <= 15 */
    if (len + 1 + 10 > 15) {
        return false;
    } /* if */

    /* Check for alphanumeric + underscore only */
    for (i = 0; i < len; i++) {
        c = dat_name[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return false;
        } /* if */
    } /* for */

    return true;
} /* validate_dat_name */

/*------------------------------------------------------------------------*/

/**
 * @brief Validate field list
 *
 * Checks that field list contains comma-separated tokens matching
 * pattern [a-z_]+ with maximum 12 tokens.
 *
 * @param field_list Field list string to validate
 * @return true if valid, false otherwise
 */
static bool validate_field_list(const char *field_list)
{
    const char *pos;
    const char *token_start;
    size_t token_len;
    size_t token_count;
    size_t i;
    char c;

    if (!field_list) {
        return false;
    } /* if */

    pos = field_list;
    token_count = 0;

    while (*pos) {
        /* Skip whitespace */
        while (*pos == ' ' || *pos == '\t') {
            pos++;
        } /* while */

        if (*pos == '\0') {
            break;
        } /* if */

        /* Start of token */
        token_start = pos;

        /* Find end of token */
        while (*pos && *pos != ',') {
            pos++;
        } /* while */

        token_len = pos - token_start;

        /* Validate token */
        if (token_len == 0) {
            return false; /* Empty token */
        } /* if */

        for (i = 0; i < token_len; i++) {
            c = token_start[i];
            if (!((c >= 'a' && c <= 'z') || c == '_')) {
                return false; /* Invalid character in token */
            } /* if */
        } /* for */

        token_count++;
        if (token_count > MAX_FIELD_COUNT) {
            return false; /* Too many tokens */
        } /* if */

        /* Skip comma */
        if (*pos == ',') {
            pos++;
        } /* if */
    } /* while */

    return token_count > 0; /* Must have at least one token */
} /* validate_field_list */

/*------------------------------------------------------------------------*/
/* String Trimming Utility                                                */
/*------------------------------------------------------------------------*/

/**
 * @brief Trim whitespace from string
 *
 * Creates a new string with leading and trailing whitespace removed.
 * Preserves the original case of non-whitespace characters. Handles
 * empty or all-whitespace inputs by returning an empty string.
 *
 * @param src Source string to trim
 * @return Newly allocated trimmed string, or empty string if all whitespace
 */
char *strtrim(const char *src)
{
    const char *start;
    const char *end;
    size_t trim_len;
    char *result;

    if (!src) {
        result = whd_malloc(1);
        if (result) {
            result[0] = '\0';
        } /* if */
        return result;
    } /* if */

    /* Find start of non-whitespace */
    start = src;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    } /* while */

    /* If string is all whitespace */
    if (*start == '\0') {
        result = whd_malloc(1);
        if (result) {
            result[0] = '\0';
        } /* if */
        return result;
    } /* if */

    /* Find end of non-whitespace */
    end = src + strlen(src) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    } /* while */

    /* Calculate trimmed length */
    trim_len = end - start + 1;

    /* Allocate and copy trimmed string */
    result = whd_malloc(trim_len + 1);
    if (!result) {
        return NULL;
    } /* if */

    memcpy(result, start, trim_len);
    result[trim_len] = '\0';

    return result;
} /* strtrim */

/*------------------------------------------------------------------------*/
/* String Splitting Utility                                               */
/*------------------------------------------------------------------------*/

/**
 * @brief Split string on delimiter with validation
 *
 * Splits a string on the specified delimiter and validates that exactly
 * the expected number of parts are returned.
 *
 * @param str String to split
 * @param delimiter Delimiter character
 * @param expected_parts Expected number of parts after splitting
 * @param out_parts Pointer to receive allocated array of string parts
 * @return true if successful with correct part count, false otherwise
 */
bool validate_and_split(const char *str, char delimiter, int expected_parts, char ***out_parts)
{
    char *str_copy;
    char *pos;
    char *part_start;
    char **parts;
    int part_count;
    int i;
    size_t part_len;

    if (!str || !out_parts || expected_parts <= 0) {
        return false;
    } /* if */

    /* Create working copy */
    str_copy = whd_malloc(strlen(str) + 1);
    if (!str_copy) {
        return false;
    } /* if */
    strcpy(str_copy, str);

    /* Allocate parts array */
    parts = whd_malloc(expected_parts * sizeof(char *));
    if (!parts) {
        whd_free(str_copy);
        return false;
    } /* if */

    /* Initialize parts array */
    for (i = 0; i < expected_parts; i++) {
        parts[i] = NULL;
    } /* for */

    /* Split string */
    pos = str_copy;
    part_start = pos;
    part_count = 0;

    while (*pos && part_count < expected_parts) {
        if (*pos == delimiter) {
            /* Found delimiter - extract part */
            *pos = '\0';
            part_len = strlen(part_start);
            parts[part_count] = whd_malloc(part_len + 1);
            if (!parts[part_count]) {
                /* Cleanup on allocation failure */
                for (i = 0; i < part_count; i++) {
                    whd_free(parts[i]);
                } /* for */
                whd_free(parts);
                whd_free(str_copy);
                return false;
            } /* if */
            strcpy(parts[part_count], part_start);
            part_count++;
            part_start = pos + 1;
        } /* if */
        pos++;
    } /* while */

    /* Handle final part */
    if (part_count < expected_parts && *part_start) {
        part_len = strlen(part_start);
        parts[part_count] = whd_malloc(part_len + 1);
        if (!parts[part_count]) {
            /* Cleanup on allocation failure */
            for (i = 0; i < part_count; i++) {
                whd_free(parts[i]);
            } /* for */
            whd_free(parts);
            whd_free(str_copy);
            return false;
        } /* if */
        strcpy(parts[part_count], part_start);
        part_count++;
    } /* if */

    whd_free(str_copy);

    /* Check if we got exactly the expected number of parts */
    if (part_count != expected_parts) {
        /* Cleanup and return failure */
        for (i = 0; i < part_count; i++) {
            whd_free(parts[i]);
        } /* for */
        whd_free(parts);
        return false;
    } /* if */

    *out_parts = parts;
    return true;
} /* validate_and_split */

/*------------------------------------------------------------------------*/

/**
 * @brief Parse field list into NULL-terminated array
 *
 * Splits comma-separated field list into individual field names.
 *
 * @param field_list Comma-separated field list
 * @param out_fields Pointer to receive field array
 * @param out_count Pointer to receive field count
 * @return true if successful, false otherwise
 */
static bool parse_field_list(const char *field_list, char ***out_fields, size_t *out_count)
{
    const char *pos;
    const char *token_start;
    char **fields;
    size_t field_count;
    size_t field_capacity;
    size_t token_len;
    size_t i;

    if (!field_list || !out_fields || !out_count) {
        return false;
    } /* if */

    /* Initial capacity */
    field_capacity = 8;
    fields = whd_malloc(field_capacity * sizeof(char *));
    if (!fields) {
        return false;
    } /* if */

    pos = field_list;
    field_count = 0;

    while (*pos) {
        /* Skip whitespace */
        while (*pos == ' ' || *pos == '\t') {
            pos++;
        } /* while */

        if (*pos == '\0') {
            break;
        } /* if */

        /* Start of token */
        token_start = pos;

        /* Find end of token */
        while (*pos && *pos != ',') {
            pos++;
        } /* while */

        token_len = pos - token_start;

        /* Expand array if needed */
        if (field_count >= field_capacity) {
            field_capacity *= 2;
            fields = realloc(fields, field_capacity * sizeof(char *));
            if (!fields) {
                return false;
            } /* if */
        } /* if */

        /* Allocate and copy token */
        fields[field_count] = whd_malloc(token_len + 1);
        if (!fields[field_count]) {
            /* Cleanup on failure */
            for (i = 0; i < field_count; i++) {
                whd_free(fields[i]);
            } /* for */
            whd_free(fields);
            return false;
        } /* if */

        memcpy(fields[field_count], token_start, token_len);
        fields[field_count][token_len] = '\0';
        field_count++;

        /* Skip comma */
        if (*pos == ',') {
            pos++;
        } /* if */
    } /* while */

    /* Add NULL terminator */
    if (field_count >= field_capacity) {
        fields = realloc(fields, (field_count + 1) * sizeof(char *));
        if (!fields) {
            for (i = 0; i < field_count; i++) {
                whd_free(fields[i]);
            } /* for */
            return false;
        } /* if */
    } /* if */
    fields[field_count] = NULL;

    *out_fields = fields;
    *out_count = field_count;
    return true;
} /* parse_field_list */

/*------------------------------------------------------------------------*/
/* Main Loading Functions                                                 */
/*------------------------------------------------------------------------*/

/**
 * @brief Load pack types from INI file
 *
 * Loads and validates pack type definitions from the specified INI file.
 * Performs strict validation on all fields according to Amiga filename
 * constraints and metadata system requirements.
 *
 * @param ini_path Path to pack_types.ini file
 * @param out_count Pointer to receive number of loaded pack types
 * @return Dynamically allocated array of PackType structures, or NULL on error
 */
PackType *load_pack_types(const char *ini_path, size_t *out_count)
{
    FILE *fp;
    char line_buffer[MAX_LINE_LENGTH];
    PackType *pack_types;
    size_t pack_count;
    size_t pack_capacity;
    int line_number;
    bool in_pack_types_section;
    char *equals_pos;
    char *id_str;
    char *value_str;
    char **value_parts;
    uint8_t pack_id;

    if (!ini_path || !out_count) {
        return NULL;
    } /* if */

    /* Open INI file */
    fp = whd_fopen(ini_path, "r");
    if (!fp) {
        printf("ERROR: Cannot open pack types file: %s\n", ini_path);
        exit(1);
    } /* if */

    /* Initialize pack types array */
    pack_capacity = 8;
    pack_types = whd_malloc(pack_capacity * sizeof(PackType));
    if (!pack_types) {
        printf("ERROR: Memory allocation failed for pack types array\n");
        whd_fclose(fp);
        exit(1);
    } /* if */

    pack_count = 0;
    line_number = 0;
    in_pack_types_section = false;

    /* Read and parse file line by line */
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        line_number++;

        /* Remove trailing newline */
        line_buffer[strcspn(line_buffer, "\r\n")] = '\0';

        /* Skip empty lines and comments */
        if (line_buffer[0] == '\0' || line_buffer[0] == ';') {
            continue;
        } /* if */

        /* Check for section header */
        if (line_buffer[0] == '[') {
            in_pack_types_section = (strcmp(line_buffer, "[PackTypes]") == 0);
            continue;
        } /* if */

        /* Skip lines outside PackTypes section */
        if (!in_pack_types_section) {
            continue;
        } /* if */

        /* Find equals sign */
        equals_pos = strchr(line_buffer, '=');
        if (!equals_pos) {
            printf("ERROR: Line %d: Missing '=' separator\n", line_number);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Split into ID and value */
        *equals_pos = '\0';
        id_str = line_buffer;
        value_str = equals_pos + 1;

        /* Trim whitespace from ID */
        while (*id_str == ' ' || *id_str == '\t') {
            id_str++;
        } /* while */

        /* Trim whitespace from value */
        while (*value_str == ' ' || *value_str == '\t') {
            value_str++;
        } /* while */

        /* Validate and parse ID */
        pack_id = (uint8_t)atoi(id_str);
        if (pack_id == 0) {
            printf("ERROR: Line %d: Invalid pack ID '%s'\n", line_number, id_str);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Split value into 5 parts */
        if (!validate_and_split(value_str, '|', 5, &value_parts)) {
            printf("ERROR: Line %d: Value must have exactly 5 pipe-delimited fields\n", line_number);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Trim whitespace from display name and abbreviation */
        char *trimmed_display = strtrim(value_parts[0]);
        char *trimmed_abbrev = strtrim(value_parts[1]);

        if (!trimmed_display || !trimmed_abbrev) {
            printf("ERROR: Line %d: Memory allocation failed during trimming\n", line_number);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Replace original strings with trimmed versions */
        whd_free(value_parts[0]);
        whd_free(value_parts[1]);
        value_parts[0] = trimmed_display;
        value_parts[1] = trimmed_abbrev;

        /* Validate each field */
        if (!validate_display_name(value_parts[0])) {
            printf("ERROR: Line %d: Invalid display name '%s' (must be 1-64 printable ASCII chars)\n",
                   line_number, value_parts[0]);
            whd_fclose(fp);
            exit(1);
        } /* if */

        if (!validate_abbrev(value_parts[1])) {
            printf("ERROR: Line %d: Invalid abbreviation '%s' (must be 1-16 printable ASCII chars)\n",
                   line_number, value_parts[1]);
            whd_fclose(fp);
            exit(1);
        } /* if */

        if (!validate_search_snippet(value_parts[2])) {
            printf("ERROR: Line %d: Invalid search snippet '%s' (must be 1-128 URL-safe chars)\n",
                   line_number, value_parts[2]);
            whd_fclose(fp);
            exit(1);
        } /* if */

        if (!validate_dat_name(value_parts[3])) {
            printf("ERROR: Line %d: Invalid DAT name '%s' (must be alphanumeric+underscore, max %d chars for Amiga filename constraint)\n",
                   line_number, value_parts[3], MAX_DAT_NAME_LEN);
            whd_fclose(fp);
            exit(1);
        } /* if */

        if (!validate_field_list(value_parts[4])) {
            printf("ERROR: Line %d: Invalid field list '%s' (must be comma-separated [a-z_]+ tokens, max %d)\n",
                   line_number, value_parts[4], MAX_FIELD_COUNT);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Expand pack types array if needed */
        if (pack_count >= pack_capacity) {
            pack_capacity *= 2;
            pack_types = realloc(pack_types, pack_capacity * sizeof(PackType));
            if (!pack_types) {
                printf("ERROR: Memory reallocation failed\n");
                whd_fclose(fp);
                exit(1);
            } /* if */
        } /* if */

        /* Create new pack type */
        pack_types[pack_count].id = pack_id;
        pack_types[pack_count].display_name = value_parts[0];
        pack_types[pack_count].abbrev = value_parts[1];
        pack_types[pack_count].search_snippet = value_parts[2];
        pack_types[pack_count].dat_name = value_parts[3];

        /* Parse field list */
        if (!parse_field_list(value_parts[4], &pack_types[pack_count].field_list,
                             &pack_types[pack_count].num_fields)) {
            printf("ERROR: Line %d: Failed to parse field list\n", line_number);
            whd_fclose(fp);
            exit(1);
        } /* if */

        /* Free the field list string since we parsed it */
        whd_free(value_parts[4]);
        whd_free(value_parts); /* Free the array itself */

        pack_count++;
    } /* while */

    whd_fclose(fp);

    if (pack_count == 0) {
        printf("ERROR: No pack types found in file\n");
        whd_free(pack_types);
        exit(1);
    } /* if */

    *out_count = pack_count;
    return pack_types;
} /* load_pack_types */

/*------------------------------------------------------------------------*/

/**
 * @brief Free pack types array
 *
 * Frees all memory allocated for the pack types array including all
 * string fields and the array itself.
 *
 * @param packs Pack types array to free
 * @param count Number of pack types in array
 */
void free_pack_types(PackType *packs, size_t count)
{
    size_t i, j;

    if (!packs) {
        return;
    } /* if */

    for (i = 0; i < count; i++) {
        whd_free(packs[i].display_name);
        whd_free(packs[i].abbrev);
        whd_free(packs[i].search_snippet);
        whd_free(packs[i].dat_name);

        if (packs[i].field_list) {
            for (j = 0; j < packs[i].num_fields; j++) {
                whd_free(packs[i].field_list[j]);
            } /* for */
            whd_free(packs[i].field_list);
        } /* if */
    } /* for */

    whd_free(packs);
} /* free_pack_types */

/* End of Text */
