#include "vh_score.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const VhTokenIdList *vh_parsed_list_for_field(const VhParsedScoreName *parsed, VhProfileField field)
{
    if (parsed == NULL) {
        return NULL;
    }

    switch (field) {
        case VH_PROFILE_FIELD_LANGUAGE: return &parsed->language;
        case VH_PROFILE_FIELD_CHIPSET: return &parsed->chipset;
        case VH_PROFILE_FIELD_VIDEO: return &parsed->video;
        case VH_PROFILE_FIELD_MEMORY: return &parsed->memory;
        case VH_PROFILE_FIELD_MEDIA: return &parsed->media;
        case VH_PROFILE_FIELD_SPECIAL: return &parsed->special;
        default: return NULL;
    }
}

static VhRejectCode vh_reject_code_for_field(VhProfileField field)
{
    switch (field) {
        case VH_PROFILE_FIELD_LANGUAGE: return VH_REJECT_LANGUAGE;
        case VH_PROFILE_FIELD_CHIPSET: return VH_REJECT_CHIPSET;
        case VH_PROFILE_FIELD_MEMORY: return VH_REJECT_MEMORY;
        case VH_PROFILE_FIELD_VIDEO: return VH_REJECT_VIDEO;
        case VH_PROFILE_FIELD_MEDIA: return VH_REJECT_MEDIA;
        case VH_PROFILE_FIELD_SPECIAL: return VH_REJECT_SPECIAL;
        default: return VH_REJECT_PROFILE;
    }
}

static const VhCsvFile *vh_csv_for_profile_field(const VhParseContext *ctx, VhProfileField field)
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

static const char *vh_lookup_token_text(const VhParseContext *ctx, VhProfileField field, int id)
{
    VhCsvResult result;
    const VhCsvFile *csv = vh_csv_for_profile_field(ctx, field);

    if (csv == NULL) {
        return "<unknown>";
    }

    if (vh_csv_lookup_id(csv, id, &result) && result.canonical != NULL && result.canonical[0] != '\0') {
        return result.canonical;
    }

    return "<unknown>";
}

static int vh_idlist_contains(const VhIdList *list, int id)
{
    int i;

    if (list == NULL) {
        return 0;
    }

    for (i = 0; i < list->count; ++i) {
        if (list->ids[i] == id) {
            return 1;
        }
    }

    return 0;
}

static int vh_best_include_rank(const VhTokenIdList *tokens, const VhIdList *includes)
{
    int best_rank;
    int i;

    if (tokens == NULL || includes == NULL || tokens->count == 0 || includes->count == 0) {
        return -1;
    }

    best_rank = -1;

    for (i = 0; i < tokens->count; ++i) {
        int j;
        for (j = 0; j < includes->count; ++j) {
            if ((int)tokens->ids[i] == includes->ids[j]) {
                if (best_rank < 0 || j < best_rank) {
                    best_rank = j;
                }
                break;
            }
        }
    }

    return best_rank;
}

void vh_score_candidate(const VhParsedScoreName *parsed,
                        const VhParseContext *ctx,
                        const VhProfile *profile,
                        int apply_include_weights,
                        VhScoreResult *out)
{
    int field_index;

    if (out == NULL) {
        return;
    }

    out->score = 0;
    out->rejected = 0;
    out->reject_code = VH_REJECT_NONE;
    out->reject_reason[0] = '\0';

    if (parsed == NULL || profile == NULL) {
        return;
    }

    for (field_index = 0; field_index < VH_PROFILE_FIELD_COUNT; ++field_index) {
        const VhFieldRule *rule = &profile->rules[field_index];
        const VhTokenIdList *tokens = vh_parsed_list_for_field(parsed, (VhProfileField)field_index);
        int i;

        if (tokens == NULL) {
            continue;
        }

        for (i = 0; i < tokens->count; ++i) {
            if (vh_idlist_contains(&rule->exclude, (int)tokens->ids[i])) {
                out->rejected = 1;
                out->reject_code = vh_reject_code_for_field((VhProfileField)field_index);
                snprintf(out->reject_reason, sizeof(out->reject_reason),
                    "%s excluded by profile token '%s'",
                    vh_profile_field_name((VhProfileField)field_index),
                    vh_lookup_token_text(ctx, (VhProfileField)field_index, (int)tokens->ids[i]));
                return;
            }
        }

        if (!apply_include_weights || rule->weight == 0) {
            continue;
        }

        {
            int rank = vh_best_include_rank(tokens, &rule->include);
            if (rank >= 0) {
                int field_score = rule->include.count - rank;
                out->score += (long)field_score * (long)rule->weight;
            }
        }
    }
}
