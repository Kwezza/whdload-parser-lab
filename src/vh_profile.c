#include "vh_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void vh_profile_defaults(VhProfile *profile)
{
    memset(profile, 0, sizeof(*profile));
    strcpy(profile->name, "default");
    profile->rules[VH_PROFILE_FIELD_LANGUAGE].weight = 100;
    profile->rules[VH_PROFILE_FIELD_CHIPSET].weight = 80;
    profile->rules[VH_PROFILE_FIELD_VIDEO].weight = 30;
    profile->rules[VH_PROFILE_FIELD_MEMORY].weight = 20;
    profile->rules[VH_PROFILE_FIELD_MEDIA].weight = 10;
    profile->rules[VH_PROFILE_FIELD_SPECIAL].weight = 0;
}

static const VhCsvFile *vh_profile_field_csv(const VhParseContext *ctx, VhProfileField field)
{
    if (ctx == NULL) {
        return NULL;
    }

    switch (field) {
        case VH_PROFILE_FIELD_LANGUAGE: return &ctx->language_csv;
        case VH_PROFILE_FIELD_CHIPSET: return &ctx->chipset_csv;
        case VH_PROFILE_FIELD_VIDEO: return &ctx->video_csv;
        case VH_PROFILE_FIELD_MEMORY: return &ctx->memory_csv;
        case VH_PROFILE_FIELD_MEDIA: return &ctx->media_csv;
        case VH_PROFILE_FIELD_SPECIAL: return &ctx->special_csv;
        default: return NULL;
    }
}

const char *vh_profile_field_name(VhProfileField field)
{
    switch (field) {
        case VH_PROFILE_FIELD_LANGUAGE: return "language";
        case VH_PROFILE_FIELD_CHIPSET: return "chipset";
        case VH_PROFILE_FIELD_VIDEO: return "video";
        case VH_PROFILE_FIELD_MEMORY: return "memory";
        case VH_PROFILE_FIELD_MEDIA: return "media";
        case VH_PROFILE_FIELD_SPECIAL: return "special";
        default: return "unknown";
    }
}

static void vh_idlist_add_unresolved(VhIdList *list, const char *token)
{
    size_t len;

    if (list == NULL || token == NULL || list->unresolved_count >= VH_PROFILE_MAX_UNRESOLVED) {
        return;
    }

    len = strlen(token);
    if (len >= VH_PROFILE_MAX_TOKEN_TEXT) {
        len = VH_PROFILE_MAX_TOKEN_TEXT - 1;
    }

    memcpy(list->unresolved[list->unresolved_count], token, len);
    list->unresolved[list->unresolved_count][len] = '\0';
    ++list->unresolved_count;
}

static void vh_idlist_add_resolved(VhIdList *list, int id)
{
    if (list == NULL || list->count >= VH_PROFILE_MAX_IDS) {
        return;
    }

    list->ids[list->count++] = id;
}

static void vh_profile_parse_token_list(VhIdList *list, const char *value, const VhCsvFile *csv)
{
    char work[512];
    char *cursor;

    if (list == NULL || value == NULL) {
        return;
    }

    strncpy(work, value, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';
    cursor = work;

    while (*cursor != '\0') {
        char *start = cursor;
        VhCsvResult result;

        while (*cursor != '\0' && *cursor != ',') {
            ++cursor;
        }

        if (*cursor == ',') {
            *cursor = '\0';
            ++cursor;
        }

        vh_trim_in_place(start);
        if (start[0] == '\0') {
            continue;
        }

        if (csv != NULL && vh_csv_lookup_token(csv, start, &result)) {
            vh_idlist_add_resolved(list, result.id);
        } else {
            vh_idlist_add_unresolved(list, start);
        }
    }
}

static int vh_profile_field_from_section(const char *section, VhProfileField *out_field)
{
    if (vh_stricmp(section, "Filter.language") == 0) {
        *out_field = VH_PROFILE_FIELD_LANGUAGE;
        return 1;
    }
    if (vh_stricmp(section, "Filter.chipset") == 0) {
        *out_field = VH_PROFILE_FIELD_CHIPSET;
        return 1;
    }
    if (vh_stricmp(section, "Filter.video") == 0) {
        *out_field = VH_PROFILE_FIELD_VIDEO;
        return 1;
    }
    if (vh_stricmp(section, "Filter.memory") == 0) {
        *out_field = VH_PROFILE_FIELD_MEMORY;
        return 1;
    }
    if (vh_stricmp(section, "Filter.media") == 0) {
        *out_field = VH_PROFILE_FIELD_MEDIA;
        return 1;
    }
    if (vh_stricmp(section, "Filter.special") == 0) {
        *out_field = VH_PROFILE_FIELD_SPECIAL;
        return 1;
    }
    return 0;
}

static void vh_profile_print_unresolved(const VhProfile *profile)
{
    int i;

    for (i = 0; i < VH_PROFILE_FIELD_COUNT; ++i) {
        int j;
        const VhIdList *inc = &profile->rules[i].include;
        const VhIdList *exc = &profile->rules[i].exclude;

        for (j = 0; j < inc->unresolved_count; ++j) {
            fprintf(stderr, "Warning: unresolved include token for %s: %s\n",
                vh_profile_field_name((VhProfileField)i), inc->unresolved[j]);
        }

        for (j = 0; j < exc->unresolved_count; ++j) {
            fprintf(stderr, "Warning: unresolved exclude token for %s: %s\n",
                vh_profile_field_name((VhProfileField)i), exc->unresolved[j]);
        }
    }
}

int vh_profile_load(VhProfile *profile, const char *path, const VhParseContext *ctx)
{
    FILE *fp;
    char line[512];
    char section[64];

    if (profile == NULL || path == NULL || ctx == NULL) {
        return 0;
    }

    vh_profile_defaults(profile);
    section[0] = '\0';

    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;

        vh_trim_in_place(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end != NULL) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
                vh_trim_in_place(section);
            }
            continue;
        }

        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }

        *eq = '\0';
        ++eq;
        vh_trim_in_place(line);
        vh_trim_in_place(eq);

        if (vh_stricmp(section, "Profile") == 0 && vh_stricmp(line, "name") == 0) {
            strncpy(profile->name, eq, sizeof(profile->name) - 1);
            profile->name[sizeof(profile->name) - 1] = '\0';
            continue;
        }

        if (vh_stricmp(section, "Scoring") == 0) {
            int i;
            for (i = 0; i < VH_PROFILE_FIELD_COUNT; ++i) {
                char key[64];
                snprintf(key, sizeof(key), "weight.%s", vh_profile_field_name((VhProfileField)i));
                if (vh_stricmp(line, key) == 0) {
                    profile->rules[i].weight = atoi(eq);
                    break;
                }
            }
            continue;
        }

        {
            VhProfileField field;
            if (vh_profile_field_from_section(section, &field)) {
                const VhCsvFile *csv = vh_profile_field_csv(ctx, field);
                if (vh_stricmp(line, "include") == 0) {
                    vh_profile_parse_token_list(&profile->rules[field].include, eq, csv);
                } else if (vh_stricmp(line, "exclude") == 0) {
                    vh_profile_parse_token_list(&profile->rules[field].exclude, eq, csv);
                }
            }
        }
    }

    fclose(fp);
    vh_profile_print_unresolved(profile);
    return 1;
}

void vh_profile_free(VhProfile *profile)
{
    if (profile != NULL) {
        memset(profile, 0, sizeof(*profile));
    }
}
