#include "vh_group.h"
#include "vh_memtrack.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "vh_score.h"

#define VH_OLD_MODEL_CANDIDATE_STRUCT_SIZE_BYTES 4908UL

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

static const char *vh_candidate_archive_name(const VhCandidateList *list, const VhCandidate *candidate)
{
    const char *name;

    if (list == NULL || candidate == NULL) {
        return "";
    }

    name = vh_string_pool_get(&list->strings, candidate->archive_name_off);
    return (name != NULL) ? name : "";
}

static const char *vh_candidate_group_key(const VhCandidateList *list, const VhCandidate *candidate)
{
    const char *group_key;

    if (list == NULL || candidate == NULL) {
        return "";
    }

    group_key = vh_string_pool_get(&list->strings, candidate->group_key_off);
    return (group_key != NULL) ? group_key : "";
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

static int vh_candidate_list_append(VhCandidateList *list, const VhCandidate *candidate)
{
    VhCandidate *new_items;
    int new_capacity;

    if (list->count == list->capacity) {
        new_capacity = (list->capacity == 0) ? 64 : list->capacity * 2;
        new_items = (VhCandidate *)vh_realloc_tag(list->items,
                              (size_t)new_capacity * sizeof(VhCandidate),
                              "candidate_array");
        if (new_items == NULL) {
            return 0;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = *candidate;
    return 1;
}

int vh_group_load_candidates(VhCandidateList *list, const char *listfile, const VhParseContext *ctx)
{
    FILE *fp;
    char line[512];
    char group_key[VH_MAX_TITLE_TEXT];
    unsigned long group_hash;
    int line_index;

    if (list == NULL || listfile == NULL || ctx == NULL) {
        return 0;
    }

    memset(list, 0, sizeof(*list));

    if (!vh_string_pool_init_tag(&list->strings, "candidate_string_pool")) {
        return 0;
    }

    list->parse_ctx = ctx;

    fp = fopen(listfile, "r");
    if (fp == NULL) {
        vh_string_pool_free(&list->strings);
        return 0;
    }

    line_index = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        VhCandidate candidate;

        vh_trim_in_place(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        memset(&candidate, 0, sizeof(candidate));

        if (!vh_parse_group_key(line, group_key, sizeof(group_key), &group_hash)) {
            continue;
        }

        if (!vh_string_pool_add(&list->strings, line, &candidate.archive_name_off) ||
            !vh_string_pool_add(&list->strings, group_key, &candidate.group_key_off)) {
            fclose(fp);
            vh_group_free_candidates(list);
            return 0;
        }

        candidate.group_hash = group_hash;
        candidate.original_index = line_index++;

        if (!vh_candidate_list_append(list, &candidate)) {
            fclose(fp);
            vh_group_free_candidates(list);
            return 0;
        }
    }

    fclose(fp);
    return 1;
}

void vh_group_free_candidates(VhCandidateList *list)
{
    if (list == NULL) {
        return;
    }

    vh_free(list->items);
    vh_string_pool_free(&list->strings);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->parse_ctx = NULL;
}

static int vh_parse_candidate(const VhCandidateList *list, const VhCandidate *candidate, VhParsedName *out)
{
    const char *archive_name;

    if (list == NULL || candidate == NULL || out == NULL || list->parse_ctx == NULL) {
        return 0;
    }

    archive_name = vh_candidate_archive_name(list, candidate);
    if (archive_name[0] == '\0') {
        return 0;
    }

    return vh_parse_filename(list->parse_ctx, archive_name, out);
}

static int vh_parse_candidate_score(const VhCandidateList *list, const VhCandidate *candidate, VhParsedScoreName *out)
{
    const char *archive_name;

    if (list == NULL || candidate == NULL || out == NULL || list->parse_ctx == NULL) {
        return 0;
    }

    archive_name = vh_candidate_archive_name(list, candidate);
    if (archive_name[0] == '\0') {
        return 0;
    }

    return vh_parse_filename_score(list->parse_ctx, archive_name, out);
}

static int vh_compare_index_by_group(const VhCandidateList *list, int ia, int ib)
{
    int cmp;

    if (list->items[ia].group_hash < list->items[ib].group_hash) {
        return -1;
    }
    if (list->items[ia].group_hash > list->items[ib].group_hash) {
        return 1;
    }

    cmp = strcmp(vh_candidate_group_key(list, &list->items[ia]),
                 vh_candidate_group_key(list, &list->items[ib]));

    if (cmp != 0) {
        return cmp;
    }

    if (list->items[ia].original_index < list->items[ib].original_index) {
        return -1;
    }
    if (list->items[ia].original_index > list->items[ib].original_index) {
        return 1;
    }
    return 0;
}

static int vh_is_same_group(const VhCandidateList *list, int ia, int ib)
{
    const VhCandidate *a;
    const VhCandidate *b;

    if (list == NULL || ia < 0 || ib < 0 || ia >= list->count || ib >= list->count) {
        return 0;
    }

    a = &list->items[ia];
    b = &list->items[ib];

    if (a->group_hash != b->group_hash) {
        return 0;
    }

    return strcmp(vh_candidate_group_key(list, a), vh_candidate_group_key(list, b)) == 0;
}

static void vh_swap_int(int *a, int *b)
{
    int tmp;

    tmp = *a;
    *a = *b;
    *b = tmp;
}

static void vh_heap_sift_down_order_by_group(const VhCandidateList *list,
                                             int *order,
                                             int heap_size,
                                             int root)
{
    int largest;

    largest = root;

    for (;;) {
        int left;
        int right;

        left = (largest * 2) + 1;
        right = left + 1;

        if (left < heap_size &&
            vh_compare_index_by_group(list, order[left], order[largest]) > 0) {
            largest = left;
        }

        if (right < heap_size &&
            vh_compare_index_by_group(list, order[right], order[largest]) > 0) {
            largest = right;
        }

        if (largest == root) {
            break;
        }

        vh_swap_int(&order[root], &order[largest]);
        root = largest;
    }
}

static void vh_sort_order_by_group(const VhCandidateList *list, int *order, int count)
{
    int i;

    if (list == NULL || order == NULL || count <= 1) {
        return;
    }

    i = (count / 2) - 1;
    while (i >= 0) {
        vh_heap_sift_down_order_by_group(list, order, count, i);
        --i;
    }

    i = count - 1;
    while (i > 0) {
        vh_swap_int(&order[0], &order[i]);
        vh_heap_sift_down_order_by_group(list, order, i, 0);
        --i;
    }
}

int vh_group_select_best(VhCandidateList *list, const VhProfile *profile)
{
    int *order;
    int i;

    if (list == NULL || profile == NULL || list->count <= 0) {
        return 0;
    }

    order = (int *)vh_malloc_tag((size_t)list->count * sizeof(int), "order_array");
    if (order == NULL) {
        return 0;
    }

    for (i = 0; i < list->count; ++i) {
        list->items[i].selected = 0;
        list->items[i].rejected = 0;
        list->items[i].reject_code = VH_REJECT_NONE;
        list->items[i].score = 0;
        list->items[i].reject_reason[0] = '\0';
        order[i] = i;
    }

    vh_sort_order_by_group(list, order, list->count);

    i = 0;
    while (i < list->count) {
        int run_start = i;
        int run_end = i + 1;
        int run_count;

        while (run_end < list->count &&
               vh_is_same_group(list, order[run_start], order[run_end])) {
            ++run_end;
        }

        run_count = run_end - run_start;

        if (run_count == 1) {
            VhCandidate *candidate = &list->items[order[run_start]];
            VhScoreResult score;
            VhParsedScoreName parsed;

            if (!vh_parse_candidate_score(list, candidate, &parsed)) {
                candidate->rejected = 1;
                candidate->reject_code = VH_REJECT_PARSE;
                candidate->score = 0;
                vh_safe_copy(candidate->reject_reason, sizeof(candidate->reject_reason),
                             "parse failed");
            } else {
                vh_score_candidate(&parsed, list->parse_ctx, profile, 0, &score);
                candidate->score = score.score;
                candidate->rejected = score.rejected;
                candidate->reject_code = score.reject_code;
                if (score.rejected) {
                    vh_safe_copy(candidate->reject_reason, sizeof(candidate->reject_reason), score.reject_reason);
                }
            }

            if (!candidate->rejected) {
                candidate->selected = 1;
            }

            i = run_end;
            continue;
        }

        {
            int k;
            int winner = -1;
            long best_score = 0;
            int winner_original_index = 0;

            for (k = run_start; k < run_end; ++k) {
                VhCandidate *candidate = &list->items[order[k]];
                VhScoreResult score;
                VhParsedScoreName parsed;

                if (!vh_parse_candidate_score(list, candidate, &parsed)) {
                    candidate->rejected = 1;
                    candidate->reject_code = VH_REJECT_PARSE;
                    candidate->score = 0;
                    vh_safe_copy(candidate->reject_reason, sizeof(candidate->reject_reason),
                                 "parse failed");
                    continue;
                }

                vh_score_candidate(&parsed, list->parse_ctx, profile, 1, &score);
                candidate->score = score.score;
                candidate->rejected = score.rejected;
                candidate->reject_code = score.reject_code;
                if (score.rejected) {
                    vh_safe_copy(candidate->reject_reason, sizeof(candidate->reject_reason), score.reject_reason);
                }

                if (candidate->rejected) {
                    continue;
                }

                if (winner < 0 || candidate->score > best_score ||
                    (candidate->score == best_score && candidate->original_index < winner_original_index)) {
                    winner = order[k];
                    best_score = candidate->score;
                    winner_original_index = candidate->original_index;
                }
            }

            if (winner >= 0) {
                list->items[winner].selected = 1;
            }
        }

        i = run_end;
    }

    vh_free(order);
    return 1;
}

void vh_group_print_selected(const VhCandidateList *list)
{
    int i;

    if (list == NULL) {
        return;
    }

    for (i = 0; i < list->count; ++i) {
        if (list->items[i].selected) {
            printf("%s\n", vh_candidate_archive_name(list, &list->items[i]));
        }
    }
}

static const char *vh_reject_code_text(VhRejectCode code)
{
    switch (code) {
        case VH_REJECT_LANGUAGE: return "language";
        case VH_REJECT_CHIPSET: return "chipset";
        case VH_REJECT_MEMORY: return "memory";
        case VH_REJECT_VIDEO: return "video";
        case VH_REJECT_MEDIA: return "media";
        case VH_REJECT_SPECIAL: return "special";
        case VH_REJECT_PROFILE: return "profile";
        case VH_REJECT_PARSE: return "parse";
        default: return "none";
    }
}

static void vh_collect_group_report_tokens(const VhCandidateList *list,
                                           const int *order,
                                           int run_start,
                                           int run_end,
                                           VhStringPool *special_tokens,
                                           VhStringPool *unknown_tokens)
{
    int k;

    if (list == NULL || order == NULL || special_tokens == NULL || unknown_tokens == NULL) {
        return;
    }

    for (k = run_start; k < run_end; ++k) {
        const VhCandidate *c = &list->items[order[k]];
        VhParsedName parsed;
        int j;
        unsigned long ignore_off;

        if (!vh_parse_candidate(list, c, &parsed)) {
            continue;
        }

        for (j = 0; j < parsed.special.count; ++j) {
            vh_string_pool_add(special_tokens, parsed.special.items[j].text, &ignore_off);
        }

        for (j = 0; j < parsed.unknown.count; ++j) {
            vh_string_pool_add(unknown_tokens, parsed.unknown.items[j].text, &ignore_off);
        }
    }
}

static void vh_print_pool_tokens_or_none(const VhStringPool *pool)
{
    unsigned long pos;
    int printed_any;

    if (pool == NULL || pool->size <= 1UL) {
        printf("none\n");
        return;
    }

    printed_any = 0;
    pos = 1UL;
    while (pos < pool->size) {
        const char *token = vh_string_pool_get(pool, pos);
        size_t len;

        if (token == NULL || token[0] == '\0') {
            break;
        }

        if (printed_any) {
            printf(", ");
        }
        printf("%s", token);
        printed_any = 1;

        len = strlen(token);
        pos += (unsigned long)len + 1UL;
    }

    if (!printed_any) {
        printf("none");
    }
    printf("\n");
}

static int vh_candidate_token_count(const VhCandidateList *list, const VhCandidate *candidate)
{
    VhParsedName parsed;

    if (candidate == NULL) {
        return 0;
    }

    if (!vh_parse_candidate(list, candidate, &parsed)) {
        return 0;
    }

    return parsed.chipset.count +
           parsed.language.count +
           parsed.memory.count +
           parsed.video.count +
           parsed.media.count +
           parsed.special.count +
           parsed.unknown.count;
}

#ifdef VH_AMIGA_MINIMAL
static void vh_print_memory_estimate(const VhCandidateList *list, int precomputed_largest_duplicate_group_size)
{
    (void)precomputed_largest_duplicate_group_size;
    (void)list;
    printf("Memory estimate: disabled in VH_AMIGA_MINIMAL build\n");
}
#else
static void vh_print_memory_estimate(const VhCandidateList *list, int precomputed_largest_duplicate_group_size)
{
    int i;
    size_t total_filename_chars;
    long total_tokens;
    size_t candidate_lite_array_bytes;
    size_t candidate_full_array_bytes;
    size_t order_array_bytes;
    size_t candidate_array_bytes;
    size_t sort_order_bytes;
    size_t token_storage_estimate_bytes;
    size_t string_pool_bytes;
    size_t csv_storage_estimate_bytes;
    size_t temporary_parse_struct_size_bytes;
    size_t temporary_parse_peak_estimate_bytes;
    size_t old_model_estimate_bytes_if_available;
    size_t estimated_selector_peak_bytes;
    size_t peak_memory_estimate_bytes;
    int largest_duplicate_group_size;
    unsigned long average_filename_length_x100;
    unsigned long reduction_vs_old_model_percent_x100;

    if (list == NULL || list->count <= 0) {
        return;
    }

    total_filename_chars = 0;
    total_tokens = 0;
    largest_duplicate_group_size = precomputed_largest_duplicate_group_size;
    if (largest_duplicate_group_size < 1) {
        largest_duplicate_group_size = 1;
    }

    for (i = 0; i < list->count; ++i) {
        total_filename_chars += strlen(vh_candidate_archive_name(list, &list->items[i]));
        total_tokens += vh_candidate_token_count(list, &list->items[i]);
    }

    average_filename_length_x100 =
        (unsigned long)(((total_filename_chars * (size_t)100U) + ((size_t)list->count / 2U)) /
                        (size_t)list->count);
    candidate_array_bytes = (size_t)list->count * sizeof(VhCandidate);
    sort_order_bytes = (size_t)list->count * sizeof(int);
    candidate_lite_array_bytes = (size_t)list->count * sizeof(VhCandidateLite);
    candidate_full_array_bytes = candidate_array_bytes;
    order_array_bytes = sort_order_bytes;
    token_storage_estimate_bytes = (size_t)total_tokens * sizeof(VhFieldToken);
    string_pool_bytes = (size_t)vh_string_pool_bytes_used(&list->strings);
    csv_storage_estimate_bytes = 0U;
    if (list->parse_ctx != NULL) {
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->language_csv);
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->chipset_csv);
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->video_csv);
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->media_csv);
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->memory_csv);
        csv_storage_estimate_bytes += (size_t)vh_csv_bytes_used(&list->parse_ctx->special_csv);
    }
    temporary_parse_struct_size_bytes = sizeof(VhParsedScoreName);
    temporary_parse_peak_estimate_bytes = (size_t)largest_duplicate_group_size * temporary_parse_struct_size_bytes;
    estimated_selector_peak_bytes = candidate_lite_array_bytes + order_array_bytes + string_pool_bytes;
    peak_memory_estimate_bytes = candidate_array_bytes + sort_order_bytes + token_storage_estimate_bytes + string_pool_bytes + csv_storage_estimate_bytes;
    old_model_estimate_bytes_if_available =
        ((size_t)list->count * (size_t)VH_OLD_MODEL_CANDIDATE_STRUCT_SIZE_BYTES) +
        sort_order_bytes + token_storage_estimate_bytes + csv_storage_estimate_bytes;

    reduction_vs_old_model_percent_x100 = 0UL;
    if (old_model_estimate_bytes_if_available > 0U &&
        old_model_estimate_bytes_if_available >= peak_memory_estimate_bytes) {
        reduction_vs_old_model_percent_x100 =
            (unsigned long)((((old_model_estimate_bytes_if_available - peak_memory_estimate_bytes) *
                               (size_t)10000U) +
                              (old_model_estimate_bytes_if_available / 2U)) /
                             old_model_estimate_bytes_if_available);
    }

    printf("Memory estimate:\n");
    printf("  candidate_count: %d\n", list->count);
        printf("  average_filename_length: %lu.%02lu\n",
            average_filename_length_x100 / 100UL,
            average_filename_length_x100 % 100UL);
    printf("  candidate_struct_size_bytes: %u\n", (unsigned)sizeof(VhCandidate));
    printf("  candidate_lite_struct_size_bytes: %u\n", (unsigned)sizeof(VhCandidateLite));
    printf("  id_list_struct_size_bytes: %u\n", (unsigned)sizeof(VhTokenIdList));
    printf("  parsed_name_old_struct_size_bytes: %u\n", (unsigned)sizeof(VhParsedName));
    printf("  parsed_score_struct_size_bytes: %u\n", (unsigned)sizeof(VhParsedScoreName));
    printf("  candidate_lite_array_bytes: %u\n", (unsigned)candidate_lite_array_bytes);
    printf("  candidate_full_array_bytes: %u\n", (unsigned)candidate_full_array_bytes);
    printf("  order_array_bytes: %u\n", (unsigned)order_array_bytes);
    printf("  parsed_token_count: %ld\n", total_tokens);
    printf("  parsed_token_storage_estimate_bytes: %u\n", (unsigned)token_storage_estimate_bytes);
    printf("  string_pool_bytes: %u\n", (unsigned)string_pool_bytes);
    printf("  csv_storage_estimate_bytes: %u\n", (unsigned)csv_storage_estimate_bytes);
    printf("  temporary_parse_struct_size_bytes: %u\n", (unsigned)temporary_parse_struct_size_bytes);
    printf("  largest_duplicate_group_size: %d\n", largest_duplicate_group_size);
    printf("  temporary_parse_peak_estimate_bytes: %u\n", (unsigned)temporary_parse_peak_estimate_bytes);
    printf("  estimated_selector_peak_bytes: %u\n", (unsigned)estimated_selector_peak_bytes);
    printf("  peak_memory_estimate_bytes: %u\n", (unsigned)peak_memory_estimate_bytes);
    printf("  old_model_estimate_bytes_if_available: %u\n", (unsigned)old_model_estimate_bytes_if_available);
    printf("  reduction_vs_old_model_percent: %lu.%02lu\n\n",
           reduction_vs_old_model_percent_x100 / 100UL,
           reduction_vs_old_model_percent_x100 % 100UL);
}
#endif

void vh_group_print_report(const VhCandidateList *list)
{
    int i;
    int *order;
    int largest_duplicate_group_size;

    if (list == NULL || list->count <= 0) {
        return;
    }

    order = (int *)vh_malloc_tag((size_t)list->count * sizeof(int), "report_order_array");
    if (order == NULL) {
        return;
    }

    for (i = 0; i < list->count; ++i) {
        order[i] = i;
    }

    vh_sort_order_by_group(list, order, list->count);
    largest_duplicate_group_size = 1;

    i = 0;
    while (i < list->count) {
        int run_start = i;
        int run_end = i + 1;
        int any_rejected = 0;
        int run_size;

        while (run_end < list->count &&
               vh_is_same_group(list, order[run_start], order[run_end])) {
            ++run_end;
        }

        run_size = run_end - run_start;
        if (run_size > largest_duplicate_group_size) {
            largest_duplicate_group_size = run_size;
        }

        {
            int k;
            const VhCandidate *selected_candidate = NULL;
            VhStringPool special_tokens;
            VhStringPool unknown_tokens;
            int have_group_tokens;

            have_group_tokens = vh_string_pool_init_tag(&special_tokens, "report_temp_pool") &&
                                vh_string_pool_init_tag(&unknown_tokens, "report_temp_pool");

            for (k = run_start; k < run_end; ++k) {
                const VhCandidate *c = &list->items[order[k]];
                if (c->selected) {
                    selected_candidate = c;
                }
                if (c->rejected) {
                    any_rejected = 1;
                }
            }

            if ((run_end - run_start) > 1 || any_rejected || selected_candidate == NULL) {
                printf("Group: %s\n", vh_candidate_group_key(list, &list->items[order[run_start]]));
                printf("Selected:\n");
                for (k = run_start; k < run_end; ++k) {
                    const VhCandidate *c = &list->items[order[k]];
                    if (c->selected) {
                        printf("  %s  score=%ld\n", vh_candidate_archive_name(list, c), c->score);
                    }
                }
                if (selected_candidate == NULL) {
                    printf("  none\n");
                }

                printf("Skipped:\n");
                for (k = run_start; k < run_end; ++k) {
                    const VhCandidate *c = &list->items[order[k]];
                    if (!c->selected) {
                        printf("  %s  score=%ld", vh_candidate_archive_name(list, c), c->score);
                        if (c->rejected) {
                            const char *code_text = vh_reject_code_text(c->reject_code);
                            if (c->reject_reason[0] != '\0') {
                                printf("  rejected (%s: %s)", code_text, c->reject_reason);
                            } else {
                                printf("  rejected (%s)", code_text);
                            }
                        }
                        printf("\n");
                    }
                }

                printf("Recognised special tags:\n  ");
                if (have_group_tokens) {
                    vh_collect_group_report_tokens(list, order, run_start, run_end, &special_tokens, &unknown_tokens);
                    vh_print_pool_tokens_or_none(&special_tokens);
                } else {
                    printf("none\n");
                }

                printf("Unknown tokens:\n  ");
                if (have_group_tokens) {
                    vh_print_pool_tokens_or_none(&unknown_tokens);
                } else {
                    printf("none\n");
                }
                printf("\n");
            }

            if (have_group_tokens) {
                vh_string_pool_free(&special_tokens);
                vh_string_pool_free(&unknown_tokens);
            }
        }

        i = run_end;
    }

    vh_free(order);

    vh_print_memory_estimate(list, largest_duplicate_group_size);
}

void vh_group_calculate_stats(const VhCandidateList *list,
                              int *out_group_count,
                              int *out_duplicate_group_count,
                              int *out_largest_duplicate_group_size,
                              int *out_selected_count)
{
    int i;
    int group_count;
    int duplicate_group_count;
    int largest_duplicate_group_size;
    int selected_count;
    int *order;

    group_count = 0;
    duplicate_group_count = 0;
    largest_duplicate_group_size = 0;
    selected_count = 0;

    if (list != NULL && list->count > 0) {
        for (i = 0; i < list->count; ++i) {
            if (list->items[i].selected) {
                ++selected_count;
            }
        }

        order = (int *)vh_malloc_tag((size_t)list->count * sizeof(int), "report_order_array");
        if (order != NULL) {
            for (i = 0; i < list->count; ++i) {
                order[i] = i;
            }

            vh_sort_order_by_group(list, order, list->count);

            i = 0;
            while (i < list->count) {
                int run_start;
                int run_end;
                int run_size;

                run_start = i;
                run_end = i + 1;
                while (run_end < list->count &&
                       vh_is_same_group(list, order[run_start], order[run_end])) {
                    ++run_end;
                }

                run_size = run_end - run_start;
                ++group_count;
                if (run_size > 1) {
                    ++duplicate_group_count;
                }
                if (run_size > largest_duplicate_group_size) {
                    largest_duplicate_group_size = run_size;
                }

                i = run_end;
            }

            vh_free(order);
        }
    }

    if (out_group_count != NULL) {
        *out_group_count = group_count;
    }
    if (out_duplicate_group_count != NULL) {
        *out_duplicate_group_count = duplicate_group_count;
    }
    if (out_largest_duplicate_group_size != NULL) {
        *out_largest_duplicate_group_size = largest_duplicate_group_size;
    }
    if (out_selected_count != NULL) {
        *out_selected_count = selected_count;
    }
}
