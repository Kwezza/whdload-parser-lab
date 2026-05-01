#include <platform.h>
#include "dat_parser_minimal.h"

#include <platform/platform_io.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define DAT_LINE_BUFFER_SIZE 4096
#define DAT_INITIAL_CAPACITY 256
#define DAT_MAX_FILENAME_LENGTH 1024

/**
 * @brief Trim leading and trailing ASCII whitespace in-place.
 */
static char *trim_ascii_whitespace(char *text)
{
    char *start;
    char *end;

    if (!text) {
        return NULL;
    }

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
        *end = '\0';
    }

    return text;
}

/**
 * @brief Check whether a trimmed line contains a rom tag.
 */
static bool is_rom_tag_line(const char *line)
{
    if (!line) {
        return false;
    }

    return (strstr(line, "<rom ") != NULL) ||
           (strstr(line, "<rom\t") != NULL) ||
           (strstr(line, "<rom name=") != NULL);
}

/**
 * @brief Extract an XML attribute value into a caller-provided buffer.
 */
static bool extract_xml_attribute(const char *line,
                                  const char *attribute_name,
                                  char *out_value,
                                  size_t out_capacity)
{
    char pattern[64];
    const char *match;
    const char *value_start;
    const char *value_end;
    size_t value_length;

    if (!line || !attribute_name || !out_value || out_capacity == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "%s=\"", attribute_name);
    match = strstr(line, pattern);
    if (!match) {
        return false;
    }

    value_start = match + strlen(pattern);
    value_end = strchr(value_start, '"');
    if (!value_end) {
        return false;
    }

    value_length = (size_t)(value_end - value_start);
    if (value_length >= out_capacity) {
        value_length = out_capacity - 1;
    }

    memcpy(out_value, value_start, value_length);
    out_value[value_length] = '\0';

    return (value_length > 0);
}

/**
 * @brief Append a filename to the dynamically-grown DAT filename array.
 */
static bool append_filename(char ***names,
                            size_t *count,
                            size_t *capacity,
                            const char *filename)
{
    char **new_names;
    char *copy;

    if (!names || !count || !capacity || !filename) {
        return false;
    }

    if (*count >= *capacity) {
        *capacity *= 2;
        new_names = (char **)whd_malloc((*capacity) * sizeof(char *));
        if (!new_names) {
            return false;
        }

        memcpy(new_names, *names, (*count) * sizeof(char *));
        whd_free(*names);
        *names = new_names;
    }

    copy = (char *)whd_malloc(strlen(filename) + 1);
    if (!copy) {
        return false;
    }

    strcpy(copy, filename);
    (*names)[*count] = copy;
    (*count)++;
    return true;
}

/**
 * @brief Parse a Logiqx DAT XML file and extract rom name attributes.
 */
size_t parse_dat_filenames_minimal(const char *dat_path, char ***out_names)
{
    FILE *file;
    char *line_buffer;
    char *filename_buffer;
    char **names;
    size_t count;
    size_t capacity;

    if (!dat_path || !out_names) {
        return 0;
    }

    *out_names = NULL;

    file = whd_fopen(dat_path, "r");
    if (!file) {
        return 0;
    }

    line_buffer = (char *)whd_malloc(DAT_LINE_BUFFER_SIZE);
    filename_buffer = (char *)whd_malloc(DAT_MAX_FILENAME_LENGTH);
    if (!line_buffer || !filename_buffer) {
        if (line_buffer) {
            whd_free(line_buffer);
        }
        if (filename_buffer) {
            whd_free(filename_buffer);
        }
        whd_fclose(file);
        return 0;
    }

    capacity = DAT_INITIAL_CAPACITY;
    count = 0;
    names = (char **)whd_malloc(capacity * sizeof(char *));
    if (!names) {
        whd_free(filename_buffer);
        whd_free(line_buffer);
        whd_fclose(file);
        return 0;
    }

    while (fgets(line_buffer, DAT_LINE_BUFFER_SIZE, file) != NULL) {
        char *trimmed = trim_ascii_whitespace(line_buffer);

        if (!trimmed || !is_rom_tag_line(trimmed)) {
            continue;
        }

        if (!extract_xml_attribute(trimmed, "name", filename_buffer, DAT_MAX_FILENAME_LENGTH)) {
            continue;
        }

        if (!append_filename(&names, &count, &capacity, filename_buffer)) {
            free_dat_filenames_minimal(names, count);
            whd_free(filename_buffer);
            whd_free(line_buffer);
            whd_fclose(file);
            return 0;
        }
    }

    whd_free(filename_buffer);
    whd_free(line_buffer);
    whd_fclose(file);

    if (count == 0) {
        whd_free(names);
        return 0;
    }

    *out_names = names;
    return count;
}

/**
 * @brief Free the filename array returned by parse_dat_filenames_minimal.
 */
void free_dat_filenames_minimal(char **names, size_t count)
{
    size_t i;

    if (!names) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (names[i]) {
            whd_free(names[i]);
        }
    }

    whd_free(names);
}
