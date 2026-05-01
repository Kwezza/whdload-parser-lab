/*------------------------------------------------------------------------*/
/*                                                                        *
 * prettify.c — Pure C89 cross-portable name beautification system       *
 * WHDLoad Manager - Single-source title prettification                  *
 *                                                                        *
 * Copyright © 2025 Kerry Thompson                                        *
 * SPDX-License-Identifier: MIT                                           *
 * $Id$                                                                   *
 *                                                                        */
/*------------------------------------------------------------------------*/

#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <utils/prettify.h>

/*------------------------------------------------------------------------*/
/* CONSTANTS                                                              */
/*------------------------------------------------------------------------*/
#define MAX_LINE_LENGTH 256

/*------------------------------------------------------------------------*/
/* OVERRIDE NODE STRUCTURE                                                */
/*------------------------------------------------------------------------*/
typedef struct OverrideNode {
    char                    *raw_name;     /* Original name (key) */
    char                    *pretty_name;  /* Pretty name (value) */
    struct OverrideNode     *next;         /* Next node in list */
} OverrideNode;

/*------------------------------------------------------------------------*/
/* MODULE GLOBALS                                                         */
/*------------------------------------------------------------------------*/
static OverrideNode *override_list = NULL;    /* Linked list of overrides */

/*------------------------------------------------------------------------*/
/* INTERNAL FUNCTION PROTOTYPES                                           */
/*------------------------------------------------------------------------*/
static int ci_cmp(const char *a, const char *b);
static bool add_override(const char *raw, const char *pretty);
static char *read_csv_field(char **line);
static bool load_csv_file(const char *csv_path);
static bool heuristic_prettify(const char *raw, char *buffer, size_t buffer_size);

/*------------------------------------------------------------------------*/

/**
 * @brief Case-insensitive string comparison
 * @param a First string
 * @param b Second string
 * @return int <0, 0, >0 for a<b, a==b, a>b respectively
 */
static int ci_cmp(const char *a, const char *b)
{
#ifdef _MSC_VER
    return _stricmp(a, b);
#else
    /* Portable fallback for C89 */
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
#endif
}

/*------------------------------------------------------------------------*/

/**
 * @brief Add an override entry to the linked list
 * @param raw Raw name (key)
 * @param pretty Pretty name (value)
 * @return bool true on success, false on memory failure
 */
static bool add_override(const char *raw, const char *pretty)
{
    OverrideNode *node;
    size_t raw_len;
    size_t pretty_len;

    if (!raw || !pretty) {
        return false;
    }

    raw_len = strlen(raw);
    pretty_len = strlen(pretty);

    /* Allocate node */
    node = (OverrideNode *)malloc(sizeof(OverrideNode));
    if (!node) {
        return false;
    }

    /* Allocate and copy strings */
    node->raw_name = (char *)malloc(raw_len + 1);
    node->pretty_name = (char *)malloc(pretty_len + 1);

    if (!node->raw_name || !node->pretty_name) {
        free(node->raw_name);
        free(node->pretty_name);
        free(node);
        return false;
    }

    strcpy(node->raw_name, raw);
    strcpy(node->pretty_name, pretty);

    /* Insert at head of list */
    node->next = override_list;
    override_list = node;

    return true;
}

/*------------------------------------------------------------------------*/

/**
 * @brief Read next CSV field from line
 * @param line Pointer to line pointer (modified)
 * @return char* Field contents or NULL if end of line
 */
static char *read_csv_field(char **line)
{
    char *start;
    char *end;

    if (!line || !*line || !**line) {
        return NULL;
    }

    start = *line;

    /* Find comma or end of line */
    end = start;
    while (*end && *end != ',' && *end != '\r' && *end != '\n') {
        end++;
    }

    /* Null-terminate field */
    if (*end) {
        *end = '\0';
        *line = end + 1;
    } else {
        *line = end;
    }

    return start;
}

/*------------------------------------------------------------------------*/

/**
 * @brief Load CSV file and populate override list
 * @param csv_path Path to CSV file
 * @return bool true on success, false on failure
 */
static bool load_csv_file(const char *csv_path)
{
    FILE *file;
    char line_buffer[MAX_LINE_LENGTH];
    char *raw_field;
    char *pretty_field;
    char *line_ptr;

    file = fopen(csv_path, "r");
    if (!file) {
        return false;
    }

    /* Read line by line */
    while (fgets(line_buffer, sizeof(line_buffer), file)) {
        /* Skip empty lines and comments */
        if (line_buffer[0] == '\0' || line_buffer[0] == '#') {
            continue;
        }

        /* Parse CSV fields */
        line_ptr = line_buffer;
        raw_field = read_csv_field(&line_ptr);
        pretty_field = read_csv_field(&line_ptr);

        if (raw_field && pretty_field && raw_field[0] && pretty_field[0]) {
            if (!add_override(raw_field, pretty_field)) {
                fclose(file);
                return false;  /* Memory allocation failed */
            }
        }
    }

    fclose(file);
    return true;
}

/*------------------------------------------------------------------------*/

/**
 * @brief Apply heuristic prettification with CamelCase splitting
 * @param raw Raw pack name
 * @param buffer Output buffer for pretty name
 * @param buffer_size Size of output buffer
 * @return bool true if successful, false if buffer too small
 *
 * CamelCase splitting heuristic that inserts spaces before uppercase letters
 * and handles letter-digit transitions. Processing stops at underscore or dot.
 * Examples: "ApacheOverdriveDemo" -> "Apache Overdrive Demo"
 *           "Formula1GrandPrix" -> "Formula 1 Grand Prix"
 */
static bool heuristic_prettify(const char *raw, char *buffer, size_t buffer_size)
{
    const char *src;
    char *dst;
    char *end;
    bool prev_was_lower;
    bool curr_is_upper;
    bool curr_is_lower;

    if (!raw || !buffer || buffer_size < 2) {
        if (buffer && buffer_size > 0) {
            buffer[0] = '\0';
        }
        return false;
    }

    src = raw;
    dst = buffer;
    end = buffer + buffer_size - 1;  /* Leave space for null terminator */
    prev_was_lower = false;

    while (*src && dst < end) {
        if (*src == '.' || *src == '_') {
            /* Stop at first dot or underscore */
            break;
        }

        curr_is_upper = isupper((unsigned char)*src);
        curr_is_lower = islower((unsigned char)*src);

        /* Simple rule: insert space before uppercase letters that follow lowercase letters */
        if (curr_is_upper && src != raw && prev_was_lower) {
            if (dst < end) {
                *dst++ = ' ';
            } else {
                break; /* Out of space */
            }
        }

        /* Copy the character */
        if (dst < end) {
            *dst++ = *src;
        } else {
            break; /* Out of space */
        }

        /* Update state for next iteration */
        prev_was_lower = curr_is_lower;
        src++;
    }

    *dst = '\0';

    /* Return true only if we processed the entire string or stopped at a delimiter */
    /* Return false if we ran out of buffer space before reaching end or delimiter */
    if (*src == '\0' || *src == '.' || *src == '_') {
        return true;  /* Successfully processed entire string or stopped at delimiter */
    } else {
        return false; /* Ran out of buffer space with characters remaining */
    }
}

/*------------------------------------------------------------------------*/

/**
 * @brief Initialize the prettify system
 * @param csv_path Path to the name_overrides.csv file
 * @return bool true on success, false on failure
 */
bool prettify_init(const char *csv_path)
{
    /* Clear any existing overrides */
    prettify_shutdown();

    /* Load CSV file if path provided */
    if (csv_path) {
        return load_csv_file(csv_path);
    }

    return true;  /* Success even without CSV file */
}

/*------------------------------------------------------------------------*/

/**
 * @brief Get pretty name for a raw WHDLoad pack name
 * @param raw Raw pack name
 * @param buf Output buffer for pretty name
 * @param cap Capacity of output buffer
 * @return char* Pointer to buf on success, NULL on failure
 */
char *prettify_title(const char *raw, char *buf, size_t cap)
{
    OverrideNode *node;

    if (!raw || !buf || cap < 2) {
        return NULL;
    }

    /* Step 1: Check override list for CSV override */
    node = override_list;
    while (node) {
        if (ci_cmp(node->raw_name, raw) == 0) {
            /* Found override - copy to buffer */
            if (strlen(node->pretty_name) < cap) {
                strcpy(buf, node->pretty_name);
                return buf;
            }
            return NULL;  /* Buffer too small */
        }
        node = node->next;
    }

    /* Step 2: Apply heuristic transformation */
    if (heuristic_prettify(raw, buf, cap)) {
        return buf[0] ? buf : NULL;
    } else {
        return NULL;  /* Buffer too small */
    }
}

/*------------------------------------------------------------------------*/

/**
 * @brief Free all resources used by the prettify system
 */
void prettify_shutdown(void)
{
    OverrideNode *node;
    OverrideNode *next;

    /* Free all override nodes */
    node = override_list;
    while (node) {
        next = node->next;
        free(node->raw_name);
        free(node->pretty_name);
        free(node);
        node = next;
    }

    override_list = NULL;
}

/* End of Text */
