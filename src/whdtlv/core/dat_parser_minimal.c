#include "platform.h"
#include "dat_parser_minimal.h"

#include "whdtlv/platform/platform_io.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Forward declaration: free_dat_filenames_minimal is defined later in this
 * file and called from parse_dat_filenames_minimal. Declaration removed from
 * dat_parser_minimal.h in Phase 2 header hygiene (2026-05-11). */
void free_dat_filenames_minimal(char **names, size_t count);

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

/*------------------------------------------------------------------------*/
/* Entry-level API: parse name + size + crc from each <rom .../> tag      */

/**
 * @brief Append a DatRomEntry to the dynamically-grown entry array.
 */
static bool append_rom_entry(DatRomEntry **entries,
                             size_t *count,
                             size_t *capacity,
                             const char *filename,
                             uint32_t size_bytes,
                             uint32_t crc32_val)
{
    DatRomEntry *new_entries;
    char *name_copy;

    if (!entries || !count || !capacity || !filename) {
        return false;
    }

    if (*count >= *capacity) {
        *capacity *= 2;
        new_entries = (DatRomEntry *)whd_malloc((*capacity) * sizeof(DatRomEntry));
        if (!new_entries) {
            return false;
        }
        memcpy(new_entries, *entries, (*count) * sizeof(DatRomEntry));
        whd_free(*entries);
        *entries = new_entries;
    }

    name_copy = (char *)whd_malloc(strlen(filename) + 1);
    if (!name_copy) {
        return false;
    }
    strcpy(name_copy, filename);

    (*entries)[*count].name       = name_copy;
    (*entries)[*count].size_bytes = size_bytes;
    (*entries)[*count].crc32      = crc32_val;
    (*count)++;
    return true;
}

/**
 * @brief Parse a hex CRC string (up to 8 hex digits) into a uint32_t.
 *        Returns 0 and prints a warning if the string is missing or malformed.
 */
static uint32_t parse_crc_attribute(const char *crc_str, const char *filename)
{
    unsigned long val;
    char *end_ptr;

    if (!crc_str || crc_str[0] == '\0') {
        fprintf(stderr, "WARNING: missing crc attribute for '%s' -- using 0\n",
                filename ? filename : "(unknown)");
        return 0;
    }

    val = strtoul(crc_str, &end_ptr, 16);
    if (end_ptr == crc_str || *end_ptr != '\0') {
        fprintf(stderr, "WARNING: malformed crc '%s' for '%s' -- using 0\n",
                crc_str, filename ? filename : "(unknown)");
        return 0;
    }

    return (uint32_t)val;
}

/**
 * @brief Parse a decimal size string into a uint32_t byte count.
 *        Returns 0 and prints a warning if the string is missing or malformed.
 */
static uint32_t parse_size_attribute(const char *size_str, const char *filename)
{
    unsigned long val;
    char *end_ptr;

    if (!size_str || size_str[0] == '\0') {
        fprintf(stderr, "WARNING: missing size attribute for '%s' -- using 0\n",
                filename ? filename : "(unknown)");
        return 0;
    }

    val = strtoul(size_str, &end_ptr, 10);
    if (end_ptr == size_str || *end_ptr != '\0') {
        fprintf(stderr, "WARNING: malformed size '%s' for '%s' -- using 0\n",
                size_str, filename ? filename : "(unknown)");
        return 0;
    }

    return (uint32_t)val;
}

/**
 * @brief Parse a Logiqx DAT XML file and extract rom name, size, and crc attributes.
 *
 * Returns the number of entries parsed. On success, *out_entries points to a
 * heap-allocated array of DatRomEntry; caller must free with free_dat_entries_minimal.
 */
size_t parse_dat_entries_minimal(const char *dat_path, DatRomEntry **out_entries)
{
    FILE *file;
    char *line_buffer;
    char *name_buffer;
    char *size_buffer;
    char *crc_buffer;
    DatRomEntry *entries;
    size_t count;
    size_t capacity;

    if (!dat_path || !out_entries) {
        return 0;
    }

    *out_entries = NULL;

    file = whd_fopen(dat_path, "r");
    if (!file) {
        return 0;
    }

    line_buffer = (char *)whd_malloc(DAT_LINE_BUFFER_SIZE);
    name_buffer = (char *)whd_malloc(DAT_MAX_FILENAME_LENGTH);
    size_buffer = (char *)whd_malloc(64);
    crc_buffer  = (char *)whd_malloc(32);

    if (!line_buffer || !name_buffer || !size_buffer || !crc_buffer) {
        if (line_buffer) { whd_free(line_buffer); }
        if (name_buffer) { whd_free(name_buffer); }
        if (size_buffer) { whd_free(size_buffer); }
        if (crc_buffer)  { whd_free(crc_buffer);  }
        whd_fclose(file);
        return 0;
    }

    capacity = DAT_INITIAL_CAPACITY;
    count    = 0;
    entries  = (DatRomEntry *)whd_malloc(capacity * sizeof(DatRomEntry));
    if (!entries) {
        whd_free(crc_buffer);
        whd_free(size_buffer);
        whd_free(name_buffer);
        whd_free(line_buffer);
        whd_fclose(file);
        return 0;
    }

    while (fgets(line_buffer, DAT_LINE_BUFFER_SIZE, file) != NULL) {
        char *trimmed;
        uint32_t size_val;
        uint32_t crc_val;

        trimmed = trim_ascii_whitespace(line_buffer);

        if (!trimmed || !is_rom_tag_line(trimmed)) {
            continue;
        }

        if (!extract_xml_attribute(trimmed, "name", name_buffer, DAT_MAX_FILENAME_LENGTH)) {
            continue;
        }

        /* Extract size (decimal bytes) -- warn and zero on failure */
        if (!extract_xml_attribute(trimmed, "size", size_buffer, 64)) {
            size_buffer[0] = '\0';
        }
        size_val = parse_size_attribute(size_buffer, name_buffer);

        /* Extract crc (8 hex chars) -- warn and zero on failure */
        if (!extract_xml_attribute(trimmed, "crc", crc_buffer, 32)) {
            crc_buffer[0] = '\0';
        }
        crc_val = parse_crc_attribute(crc_buffer, name_buffer);

        if (!append_rom_entry(&entries, &count, &capacity,
                              name_buffer, size_val, crc_val)) {
            free_dat_entries_minimal(entries, count);
            whd_free(crc_buffer);
            whd_free(size_buffer);
            whd_free(name_buffer);
            whd_free(line_buffer);
            whd_fclose(file);
            return 0;
        }
    }

    whd_free(crc_buffer);
    whd_free(size_buffer);
    whd_free(name_buffer);
    whd_free(line_buffer);
    whd_fclose(file);

    if (count == 0) {
        whd_free(entries);
        return 0;
    }

    *out_entries = entries;
    return count;
}

/**
 * @brief Free the entry array returned by parse_dat_entries_minimal.
 */
void free_dat_entries_minimal(DatRomEntry *entries, size_t count)
{
    size_t i;

    if (!entries) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (entries[i].name) {
            whd_free(entries[i].name);
        }
    }

    whd_free(entries);
}
