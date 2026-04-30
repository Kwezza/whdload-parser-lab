#ifndef VH_PARSE_H
#define VH_PARSE_H

#include <stddef.h>

#include "vh_csv.h"

#define VH_MAX_TOKENS_PER_FIELD 8
#define VH_MAX_TOKEN_TEXT 64
#define VH_MAX_TITLE_TEXT 128
#define VH_MAX_ARCHIVE_NAME 256
#define VH_MAX_VERSION_TEXT 32
#define VH_MAX_SCORE_IDS_PER_FIELD 8

typedef struct VhFieldToken {
    int id;
    char text[VH_MAX_TOKEN_TEXT];
} VhFieldToken;

typedef struct VhTokenList {
    VhFieldToken items[VH_MAX_TOKENS_PER_FIELD];
    int count;
} VhTokenList;

typedef struct VhParsedName {
    char archive_name[VH_MAX_ARCHIVE_NAME];
    char title[VH_MAX_TITLE_TEXT];
    char group_key[VH_MAX_TITLE_TEXT];
    char version[VH_MAX_VERSION_TEXT];

    VhTokenList chipset;
    VhTokenList language;
    VhTokenList memory;
    VhTokenList video;
    VhTokenList media;
    VhTokenList special;

    VhTokenList unknown;
} VhParsedName;

typedef struct VhTokenIdList {
    unsigned short ids[VH_MAX_SCORE_IDS_PER_FIELD];
    unsigned char count;
} VhTokenIdList;

typedef struct VhParsedScoreName {
    VhTokenIdList chipset;
    VhTokenIdList language;
    VhTokenIdList memory;
    VhTokenIdList video;
    VhTokenIdList media;
    VhTokenIdList special;
    unsigned char has_unknown;
} VhParsedScoreName;

typedef struct VhParseContext {
    VhCsvFile language_csv;
    VhCsvFile chipset_csv;
    VhCsvFile video_csv;
    VhCsvFile media_csv;
    VhCsvFile memory_csv;
    VhCsvFile special_csv;
    int is_loaded;
} VhParseContext;

int vh_parse_context_load(VhParseContext *ctx, const char *defs_dir);
void vh_parse_context_free(VhParseContext *ctx);
int vh_parse_filename(const VhParseContext *ctx, const char *archive_name, VhParsedName *out);
int vh_parse_filename_score(const VhParseContext *ctx, const char *archive_name, VhParsedScoreName *out);
int vh_parse_group_key(const char *archive_name,
                       char *group_key_buf,
                       size_t group_key_buf_size,
                       unsigned long *out_hash);

#endif
