#include "vh_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VH_PATH_MAX 512
#define VH_WORK_MAX 512
#define VH_MAX_SPLIT_TOKENS 64

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

static int vh_safe_copy(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (dst == NULL || src == NULL || dst_size == 0) {
        return 0;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

static int vh_load_single_csv(VhCsvFile *csv, const char *defs_dir, const char *file_name)
{
    char path[VH_PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/%s", defs_dir, file_name) >= (int)sizeof(path)) {
        return 0;
    }

    return vh_csv_load(csv, path);
}

static void vh_token_list_add(VhTokenList *list, int id, const char *text)
{
    VhFieldToken *slot;

    if (list == NULL || text == NULL) {
        return;
    }

    if (list->count >= VH_MAX_TOKENS_PER_FIELD) {
        return;
    }

    slot = &list->items[list->count++];
    slot->id = id;
    vh_safe_copy(slot->text, sizeof(slot->text), text);
}

static void vh_token_id_list_add(VhTokenIdList *list, int id)
{
    if (list == NULL) {
        return;
    }

    if (id < 0 || id > 65535) {
        return;
    }

    if (list->count >= VH_MAX_SCORE_IDS_PER_FIELD) {
        return;
    }

    list->ids[list->count++] = (unsigned short)id;
}

static void vh_split_underscore_tokens(char *text, char **tokens, int *token_count)
{
    char *cursor;
    int count;

    *token_count = 0;
    if (text == NULL) {
        return;
    }

    cursor = text;
    count = 0;

    while (*cursor != '\0' && count < VH_MAX_SPLIT_TOKENS) {
        char *start = cursor;

        while (*cursor != '\0' && *cursor != '_') {
            ++cursor;
        }

        if (*cursor == '_') {
            *cursor = '\0';
            ++cursor;
        }

        if (start[0] != '\0') {
            tokens[count++] = start;
        }
    }

    *token_count = count;
}

static int vh_is_version_token(const char *token)
{
    const char *p;
    int has_dot;

    if (token == NULL || token[0] == '\0') {
        return 0;
    }

    p = token;
    if (*p == 'v' || *p == 'V') {
        ++p;
    }

    if (!isdigit((unsigned char)*p)) {
        return 0;
    }

    while (isdigit((unsigned char)*p)) {
        ++p;
    }

    if (*p != '.') {
        return 0;
    }

    has_dot = 1;
    while (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
    }

    if (!has_dot) {
        return 0;
    }

    return *p == '\0';
}

static void vh_make_group_key(const char *title, char *group_key, size_t group_key_size)
{
    size_t i;
    size_t out;

    out = 0;
    if (group_key_size == 0) {
        return;
    }

    for (i = 0; title != NULL && title[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)title[i];

        if (isalnum(ch)) {
            if (out + 1 < group_key_size) {
                group_key[out++] = (char)tolower(ch);
            }
        }
    }

    group_key[out] = '\0';
}

static unsigned long vh_group_key_hash32(const char *group_key)
{
    const unsigned char *p;
    unsigned long hash = 2166136261UL;

    if (group_key == NULL) {
        return 0UL;
    }

    p = (const unsigned char *)group_key;
    while (*p != '\0') {
        hash ^= (unsigned long)(*p++);
        hash *= 16777619UL;
        hash &= 0xFFFFFFFFUL;
    }

    return hash;
}

static int vh_try_language_compact_token(const VhCsvFile *language_csv, const char *token, VhTokenList *language)
{
    size_t len;
    size_t i;

    len = strlen(token);
    if (len < 4 || (len % 2) != 0) {
        return 0;
    }

    for (i = 0; i < len; i += 2) {
        char chunk[3];
        VhCsvResult result;

        chunk[0] = token[i];
        chunk[1] = token[i + 1];
        chunk[2] = '\0';

        if (!vh_csv_lookup_token(language_csv, chunk, &result)) {
            return 0;
        }
    }

    for (i = 0; i < len; i += 2) {
        char chunk[3];
        VhCsvResult result;

        chunk[0] = token[i];
        chunk[1] = token[i + 1];
        chunk[2] = '\0';

        if (vh_csv_lookup_token(language_csv, chunk, &result)) {
            vh_token_list_add(language, result.id, result.canonical != NULL ? result.canonical : chunk);
        }
    }

    return 1;
}

static int vh_try_language_compact_token_ids(const VhCsvFile *language_csv, const char *token, VhTokenIdList *language)
{
    size_t len;
    size_t i;

    len = strlen(token);
    if (len < 4 || (len % 2) != 0) {
        return 0;
    }

    for (i = 0; i < len; i += 2) {
        char chunk[3];
        VhCsvResult result;

        chunk[0] = token[i];
        chunk[1] = token[i + 1];
        chunk[2] = '\0';

        if (!vh_csv_lookup_token(language_csv, chunk, &result)) {
            return 0;
        }
    }

    for (i = 0; i < len; i += 2) {
        char chunk[3];
        VhCsvResult result;

        chunk[0] = token[i];
        chunk[1] = token[i + 1];
        chunk[2] = '\0';

        if (vh_csv_lookup_token(language_csv, chunk, &result)) {
            vh_token_id_list_add(language, result.id);
        }
    }

    return 1;
}

static int vh_try_match_field(const VhCsvFile *csv, const char *token, VhTokenList *list)
{
    VhCsvResult result;

    if (!vh_csv_lookup_token(csv, token, &result)) {
        return 0;
    }

    vh_token_list_add(list, result.id, result.canonical != NULL ? result.canonical : token);
    return 1;
}

static int vh_try_match_field_id(const VhCsvFile *csv, const char *token, VhTokenIdList *list)
{
    VhCsvResult result;

    if (!vh_csv_lookup_token(csv, token, &result)) {
        return 0;
    }

    vh_token_id_list_add(list, result.id);
    return 1;
}

int vh_parse_context_load(VhParseContext *ctx, const char *defs_dir)
{
    if (ctx == NULL || defs_dir == NULL) {
        return 0;
    }

    memset(ctx, 0, sizeof(*ctx));

    if (!vh_load_single_csv(&ctx->language_csv, defs_dir, "Language.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    if (!vh_load_single_csv(&ctx->chipset_csv, defs_dir, "Chipset.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    if (!vh_load_single_csv(&ctx->video_csv, defs_dir, "Video.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    if (!vh_load_single_csv(&ctx->media_csv, defs_dir, "Media.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    if (!vh_load_single_csv(&ctx->memory_csv, defs_dir, "Memory.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    if (!vh_load_single_csv(&ctx->special_csv, defs_dir, "Special.csv")) {
        vh_parse_context_free(ctx);
        return 0;
    }

    ctx->is_loaded = 1;
    return 1;
}

void vh_parse_context_free(VhParseContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    vh_csv_free(&ctx->language_csv);
    vh_csv_free(&ctx->chipset_csv);
    vh_csv_free(&ctx->video_csv);
    vh_csv_free(&ctx->media_csv);
    vh_csv_free(&ctx->memory_csv);
    vh_csv_free(&ctx->special_csv);
    ctx->is_loaded = 0;
}

int vh_parse_filename(const VhParseContext *ctx, const char *archive_name, VhParsedName *out)
{
    char work[VH_WORK_MAX];
    char *tokens[VH_MAX_SPLIT_TOKENS];
    int token_count;
    int i;
    size_t work_len;

    if (ctx == NULL || out == NULL || archive_name == NULL || !ctx->is_loaded) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    vh_safe_copy(out->archive_name, sizeof(out->archive_name), archive_name);

    vh_safe_copy(work, sizeof(work), archive_name);
    work_len = strlen(work);

    if (work_len >= 4) {
        char *ext = work + (work_len - 4);
        if (vh_stricmp(ext, ".lha") == 0 || vh_stricmp(ext, ".lzx") == 0) {
            *ext = '\0';
        }
    }

    vh_split_underscore_tokens(work, tokens, &token_count);
    if (token_count <= 0) {
        return 0;
    }

    vh_safe_copy(out->title, sizeof(out->title), tokens[0]);
    vh_make_group_key(out->title, out->group_key, sizeof(out->group_key));

    for (i = 1; i < token_count; ++i) {
        const char *token = tokens[i];

        if (out->version[0] == '\0' && vh_is_version_token(token)) {
            vh_safe_copy(out->version, sizeof(out->version), token);
            continue;
        }

        if (vh_try_match_field(&ctx->language_csv, token, &out->language)) {
            continue;
        }

        if (vh_try_language_compact_token(&ctx->language_csv, token, &out->language)) {
            continue;
        }

        if (vh_try_match_field(&ctx->chipset_csv, token, &out->chipset)) {
            continue;
        }

        if (vh_try_match_field(&ctx->video_csv, token, &out->video)) {
            continue;
        }

        if (vh_try_match_field(&ctx->media_csv, token, &out->media)) {
            continue;
        }

        if (vh_try_match_field(&ctx->memory_csv, token, &out->memory)) {
            continue;
        }

        if (vh_try_match_field(&ctx->special_csv, token, &out->special)) {
            continue;
        }

        vh_token_list_add(&out->unknown, 0, token);
    }

    return 1;
}

int vh_parse_filename_score(const VhParseContext *ctx, const char *archive_name, VhParsedScoreName *out)
{
    char work[VH_WORK_MAX];
    char *tokens[VH_MAX_SPLIT_TOKENS];
    int token_count;
    int i;
    size_t work_len;

    if (ctx == NULL || out == NULL || archive_name == NULL || !ctx->is_loaded) {
        return 0;
    }

    memset(out, 0, sizeof(*out));

    vh_safe_copy(work, sizeof(work), archive_name);
    work_len = strlen(work);

    if (work_len >= 4) {
        char *ext = work + (work_len - 4);
        if (vh_stricmp(ext, ".lha") == 0 || vh_stricmp(ext, ".lzx") == 0) {
            *ext = '\0';
        }
    }

    vh_split_underscore_tokens(work, tokens, &token_count);
    if (token_count <= 0) {
        return 0;
    }

    for (i = 1; i < token_count; ++i) {
        const char *token = tokens[i];

        if (vh_is_version_token(token)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->language_csv, token, &out->language)) {
            continue;
        }

        if (vh_try_language_compact_token_ids(&ctx->language_csv, token, &out->language)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->chipset_csv, token, &out->chipset)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->video_csv, token, &out->video)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->media_csv, token, &out->media)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->memory_csv, token, &out->memory)) {
            continue;
        }

        if (vh_try_match_field_id(&ctx->special_csv, token, &out->special)) {
            continue;
        }

        out->has_unknown = 1;
    }

    return 1;
}

int vh_parse_group_key(const char *archive_name,
                       char *group_key_buf,
                       size_t group_key_buf_size,
                       unsigned long *out_hash)
{
    char work[VH_WORK_MAX];
    size_t len;
    char *sep;

    if (archive_name == NULL || group_key_buf == NULL || group_key_buf_size == 0) {
        return 0;
    }

    vh_safe_copy(work, sizeof(work), archive_name);
    len = strlen(work);

    if (len >= 4) {
        char *ext = work + (len - 4);
        if (vh_stricmp(ext, ".lha") == 0 || vh_stricmp(ext, ".lzx") == 0) {
            *ext = '\0';
        }
    }

    sep = strchr(work, '_');
    if (sep != NULL) {
        *sep = '\0';
    }

    vh_make_group_key(work, group_key_buf, group_key_buf_size);

    if (group_key_buf[0] == '\0') {
        if (out_hash != NULL) {
            *out_hash = 0UL;
        }
        return 0;
    }

    if (out_hash != NULL) {
        *out_hash = vh_group_key_hash32(group_key_buf);
    }

    return 1;
}
