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
