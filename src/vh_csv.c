#include "vh_csv.h"
#include "vh_memtrack.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VH_MAX_CSV_LINE 2048
#define VH_MAX_CSV_COLS 8

static int vh_char_icmp(char a, char b)
{
    return tolower((unsigned char)a) - tolower((unsigned char)b);
}

static int vh_stricmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int diff = vh_char_icmp(*a, *b);
        if (diff != 0) {
            return diff;
        }
        ++a;
        ++b;
    }
    return vh_char_icmp(*a, *b);
}

static const char *vh_csv_entry_token(const VhCsvFile *csv, const VhCsvEntry *entry)
{
    const char *token;

    if (csv == NULL || entry == NULL) {
        return NULL;
    }

    token = vh_string_pool_get(&csv->strings, entry->token_off);
    return (token != NULL) ? token : "";
}

static const char *vh_csv_entry_description(const VhCsvFile *csv, const VhCsvEntry *entry)
{
    const char *description;

    if (csv == NULL || entry == NULL) {
        return NULL;
    }

    description = vh_string_pool_get(&csv->strings, entry->description_off);
    return (description != NULL) ? description : "";
}

static void vh_trim_in_place(char *text)
{
    size_t len;
    size_t start;
    size_t end;

    if (text == NULL || text[0] == '\0') {
        return;
    }

    len = strlen(text);
    start = 0;
    while (start < len && isspace((unsigned char)text[start])) {
        ++start;
    }

    end = len;
    while (end > start && isspace((unsigned char)text[end - 1])) {
        --end;
    }

    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static int vh_csv_split_line(char *line, char **cols, int max_cols)
{
    int count;
    char *read_ptr;
    char *write_ptr;
    int in_quotes;

    count = 0;
    read_ptr = line;

    while (*read_ptr != '\0' && count < max_cols) {
        cols[count] = read_ptr;
        write_ptr = read_ptr;
        in_quotes = 0;

        while (*read_ptr != '\0') {
            char ch = *read_ptr++;

            if (ch == '"') {
                if (in_quotes && *read_ptr == '"') {
                    *write_ptr++ = '"';
                    ++read_ptr;
                    continue;
                }
                in_quotes = !in_quotes;
                continue;
            }

            if (ch == ',' && !in_quotes) {
                break;
            }

            *write_ptr++ = ch;
        }

        *write_ptr = '\0';
        vh_trim_in_place(cols[count]);
        ++count;
    }

    return count;
}

static int vh_csv_append_entry(VhCsvFile *csv, const VhCsvEntry *entry)
{
    VhCsvEntry *new_entries;
    int new_capacity;

    if (csv->count == csv->capacity) {
        new_capacity = (csv->capacity == 0) ? 32 : csv->capacity * 2;
        new_entries = (VhCsvEntry *)vh_realloc_tag(csv->entries,
                               (size_t)new_capacity * sizeof(VhCsvEntry),
                               "csv_entry_array");
        if (new_entries == NULL) {
            return 0;
        }
        csv->entries = new_entries;
        csv->capacity = new_capacity;
    }

    csv->entries[csv->count++] = *entry;
    return 1;
}

static const char *vh_csv_get_canonical_for_id(const VhCsvFile *csv, int id)
{
    int i;

    for (i = 0; i < csv->count; ++i) {
        if (csv->entries[i].id == id) {
            return vh_csv_entry_token(csv, &csv->entries[i]);
        }
    }

    return NULL;
}

int vh_csv_load(VhCsvFile *csv, const char *path)
{
    FILE *fp;
    char line_buf[VH_MAX_CSV_LINE];

    if (csv == NULL || path == NULL) {
        return 0;
    }

    csv->entries = NULL;
    csv->count = 0;
    csv->capacity = 0;

    if (!vh_string_pool_init_tag(&csv->strings, "csv_string_pool")) {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        vh_string_pool_free(&csv->strings);
        return 0;
    }

    while (fgets(line_buf, sizeof(line_buf), fp) != NULL) {
        char *cols[VH_MAX_CSV_COLS];
        int col_count;
        char *endptr;
        long id_value;
        VhCsvEntry entry;

        vh_trim_in_place(line_buf);
        if (line_buf[0] == '\0' || line_buf[0] == '#' || line_buf[0] == ';') {
            continue;
        }

        col_count = vh_csv_split_line(line_buf, cols, VH_MAX_CSV_COLS);
        if (col_count < 2) {
            continue;
        }

        errno = 0;
        id_value = strtol(cols[0], &endptr, 10);
        if (errno != 0 || endptr == cols[0] || *endptr != '\0') {
            continue;
        }

        entry.id = (int)id_value;
        entry.flags = (unsigned char)((col_count >= 4 && vh_stricmp(cols[3], "default") == 0) ? VH_CSV_ENTRY_DEFAULT : 0);

        if (!vh_string_pool_add(&csv->strings, cols[1], &entry.token_off) ||
            !vh_string_pool_add(&csv->strings, (col_count >= 3) ? cols[2] : "", &entry.description_off)) {
            vh_csv_free(csv);
            fclose(fp);
            return 0;
        }

        if (!vh_csv_append_entry(csv, &entry)) {
            vh_csv_free(csv);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 1;
}

void vh_csv_free(VhCsvFile *csv)
{
    if (csv == NULL) {
        return;
    }

    vh_free(csv->entries);
    vh_string_pool_free(&csv->strings);
    csv->entries = NULL;
    csv->count = 0;
    csv->capacity = 0;
}

int vh_csv_lookup_token(const VhCsvFile *csv, const char *token, VhCsvResult *out)
{
    int i;

    if (csv == NULL || token == NULL || out == NULL) {
        return 0;
    }

    for (i = 0; i < csv->count; ++i) {
        const char *entry_token = vh_csv_entry_token(csv, &csv->entries[i]);
        if (strcmp(token, entry_token) == 0) {
            out->id = csv->entries[i].id;
            out->canonical = vh_csv_get_canonical_for_id(csv, out->id);
            out->description = vh_csv_entry_description(csv, &csv->entries[i]);
            return 1;
        }
    }

    for (i = 0; i < csv->count; ++i) {
        const char *entry_token = vh_csv_entry_token(csv, &csv->entries[i]);
        if (vh_stricmp(token, entry_token) == 0) {
            out->id = csv->entries[i].id;
            out->canonical = vh_csv_get_canonical_for_id(csv, out->id);
            out->description = vh_csv_entry_description(csv, &csv->entries[i]);
            return 1;
        }
    }

    return 0;
}

int vh_csv_lookup_id(const VhCsvFile *csv, int id, VhCsvResult *out)
{
    int i;

    if (csv == NULL || out == NULL) {
        return 0;
    }

    for (i = 0; i < csv->count; ++i) {
        if (csv->entries[i].id == id) {
            out->id = id;
            out->canonical = vh_csv_get_canonical_for_id(csv, id);
            out->description = vh_csv_entry_description(csv, &csv->entries[i]);
            return (out->canonical != NULL);
        }
    }

    return 0;
}

int vh_csv_get_default(const VhCsvFile *csv, VhCsvResult *out)
{
    int i;

    if (csv == NULL || out == NULL || csv->count <= 0) {
        return 0;
    }

    for (i = 0; i < csv->count; ++i) {
        if ((csv->entries[i].flags & VH_CSV_ENTRY_DEFAULT) != 0) {
            out->id = csv->entries[i].id;
            out->canonical = vh_csv_get_canonical_for_id(csv, out->id);
            out->description = vh_csv_entry_description(csv, &csv->entries[i]);
            return 1;
        }
    }

    out->id = csv->entries[0].id;
    out->canonical = vh_csv_get_canonical_for_id(csv, out->id);
    out->description = vh_csv_entry_description(csv, &csv->entries[0]);
    return 1;
}

unsigned long vh_csv_bytes_used(const VhCsvFile *csv)
{
    unsigned long entry_bytes;
    unsigned long string_bytes;

    if (csv == NULL) {
        return 0UL;
    }

    entry_bytes = (unsigned long)csv->count * (unsigned long)sizeof(VhCsvEntry);
    string_bytes = vh_string_pool_bytes_used(&csv->strings);
    return entry_bytes + string_bytes;
}
