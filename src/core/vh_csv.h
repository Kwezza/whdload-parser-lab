#ifndef VH_CSV_H
#define VH_CSV_H

#include "vh_string_pool.h"

#define VH_CSV_ENTRY_DEFAULT 0x01

typedef struct VhCsvEntry {
    int id;
    unsigned long token_off;
    unsigned long description_off;
    unsigned char flags;
} VhCsvEntry;

typedef struct VhCsvFile {
    VhCsvEntry *entries;
    int count;
    int capacity;
    VhStringPool strings;
} VhCsvFile;

typedef struct VhCsvResult {
    int id;
    const char *canonical;
    const char *description;
} VhCsvResult;

int vh_csv_load(VhCsvFile *csv, const char *path);
void vh_csv_free(VhCsvFile *csv);
int vh_csv_lookup_token(const VhCsvFile *csv, const char *token, VhCsvResult *out);
int vh_csv_lookup_id(const VhCsvFile *csv, int id, VhCsvResult *out);
int vh_csv_get_default(const VhCsvFile *csv, VhCsvResult *out);
unsigned long vh_csv_bytes_used(const VhCsvFile *csv);

#endif
