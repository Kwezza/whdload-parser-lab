#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vh_csv.h"
#include "vh_fields.h"
#include "vh_group.h"
#include "vh_memtrack.h"
#include "vh_parse.h"
#include "vh_profile.h"

#define VH_PATH_MAX 512
#define VH_OLD_MODEL_CANDIDATE_STRUCT_SIZE_BYTES 4908UL

static void print_help(const char *argv0)
{
    printf("Variant Selection Harness (Milestone 3)\n");
    printf("Usage: %s [command]\n\n", argv0);
    printf("Commands:\n");
    printf("  --help                               Show this help\n");
    printf("  --parse <filename>                   Parse filename fields\n");
    printf("  --resolve <field> <token>            Resolve token via CSV lookup\n");
    printf("  --select <listfile> --profile <name> Select one candidate per group\n");
    printf("  --report <listfile> --profile <name> Report grouped scoring details\n");
    printf("  --memtrace                           Print tracked heap summary (with --select/--report)\n");
}

static void print_supported_fields(void)
{
    printf("Supported fields: Memory, Language, Chipset, Video, Media\n");
}

static int handle_resolve(const char *field_name, const char *token)
{
    VhField field;
    const char *csv_file;
    char csv_path[VH_PATH_MAX];
    VhCsvFile csv;
    VhCsvResult result;
    int matched;

    if (!vh_field_from_name(field_name, &field)) {
        fprintf(stderr, "Unknown field: %s\n", field_name);
        print_supported_fields();
        return 1;
    }

    csv_file = vh_field_csv_filename(field);
    if (csv_file == NULL) {
        fprintf(stderr, "No CSV mapping configured for field: %s\n", field_name);
        return 1;
    }

    if (snprintf(csv_path, sizeof(csv_path), "data/Defs/%s", csv_file) >= (int)sizeof(csv_path)) {
        fprintf(stderr, "CSV path too long for field: %s\n", field_name);
        return 1;
    }

    if (!vh_csv_load(&csv, csv_path)) {
        fprintf(stderr, "Failed to load CSV: %s\n", csv_path);
        return 1;
    }

    matched = vh_csv_lookup_token(&csv, token, &result);

    printf("field: %s\n", vh_field_display_name(field));
    printf("token: %s\n", token);
    printf("matched: %s\n", matched ? "yes" : "no");
    if (matched) {
        printf("id: %d\n", result.id);
        printf("canonical: %s\n", (result.canonical != NULL) ? result.canonical : "");
        printf("description: %s\n", (result.description != NULL) ? result.description : "");
    }

    vh_csv_free(&csv);
    return matched ? 0 : 1;
}

static void print_token_list(const char *label, const VhTokenList *list)
{
    int i;

    printf("%s: ", label);
    if (list == NULL || list->count == 0) {
        printf("\n");
        return;
    }

    for (i = 0; i < list->count; ++i) {
        if (i > 0) {
            printf(", ");
        }
        printf("%s", list->items[i].text);
    }
    printf("\n");
}

static int handle_parse(const char *filename)
{
    VhParseContext ctx;
    VhParsedName parsed;

    if (!vh_parse_context_load(&ctx, "data/Defs")) {
        fprintf(stderr, "Failed to load parser CSV definitions from data/Defs\n");
        return 1;
    }

    if (!vh_parse_filename(&ctx, filename, &parsed)) {
        vh_parse_context_free(&ctx);
        fprintf(stderr, "Failed to parse filename: %s\n", filename);
        return 1;
    }

    printf("archive: %s\n", parsed.archive_name);
    printf("title: %s\n", parsed.title);
    printf("group_key: %s\n", parsed.group_key);
    printf("version: %s\n", parsed.version);
    print_token_list("chipset", &parsed.chipset);
    print_token_list("memory", &parsed.memory);
    print_token_list("language", &parsed.language);
    print_token_list("video", &parsed.video);
    print_token_list("media", &parsed.media);
    print_token_list("special", &parsed.special);
    print_token_list("unknown", &parsed.unknown);

    vh_parse_context_free(&ctx);
    return 0;
}

static int build_profile_path(const char *profile_arg, char *out_path, size_t out_size)
{
    if (strchr(profile_arg, '/') != NULL || strchr(profile_arg, '\\') != NULL || strchr(profile_arg, '.') != NULL) {
        return snprintf(out_path, out_size, "%s", profile_arg) < (int)out_size;
    }
    return snprintf(out_path, out_size, "data/Profiles/%s.profile", profile_arg) < (int)out_size;
}

static void print_selected_candidates(const VhCandidateList *candidates)
{
    int i;

    if (candidates == NULL) {
        return;
    }

    for (i = 0; i < candidates->count; ++i) {
        const VhCandidate *candidate = &candidates->items[i];
        if (candidate->selected) {
            const char *name = vh_string_pool_get(&candidates->strings, candidate->archive_name_off);
            if (name != NULL) {
                printf("%s\n", name);
            }
        }
    }
}

static const char *candidate_archive_name(const VhCandidateList *list, const VhCandidate *candidate)
{
    const char *name;

    if (list == NULL || candidate == NULL) {
        return "";
    }

    name = vh_string_pool_get(&list->strings, candidate->archive_name_off);
    return (name != NULL) ? name : "";
}

static const char *candidate_group_key(const VhCandidateList *list, const VhCandidate *candidate)
{
    const char *group_key;

    if (list == NULL || candidate == NULL) {
        return "";
    }

    group_key = vh_string_pool_get(&list->strings, candidate->group_key_off);
    return (group_key != NULL) ? group_key : "";
}

static const char *reject_code_text(VhRejectCode code)
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

static int compare_index_by_group(const VhCandidateList *list, int ia, int ib)
{
    int cmp;

    if (list->items[ia].group_hash < list->items[ib].group_hash) {
        return -1;
    }
    if (list->items[ia].group_hash > list->items[ib].group_hash) {
        return 1;
    }

    cmp = strcmp(candidate_group_key(list, &list->items[ia]),
                 candidate_group_key(list, &list->items[ib]));

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

static int is_same_group(const VhCandidateList *list, int ia, int ib)
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

    return strcmp(candidate_group_key(list, a), candidate_group_key(list, b)) == 0;
}

static void swap_int(int *a, int *b)
{
    int tmp;

    tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heap_sift_down_order_by_group(const VhCandidateList *list,
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
            compare_index_by_group(list, order[left], order[largest]) > 0) {
            largest = left;
        }

        if (right < heap_size &&
            compare_index_by_group(list, order[right], order[largest]) > 0) {
            largest = right;
        }

        if (largest == root) {
            break;
        }

        swap_int(&order[root], &order[largest]);
        root = largest;
    }
}

static void sort_order_by_group(const VhCandidateList *list, int *order, int count)
{
    int i;

    if (list == NULL || order == NULL || count <= 1) {
        return;
    }

    i = (count / 2) - 1;
    while (i >= 0) {
        heap_sift_down_order_by_group(list, order, count, i);
        --i;
    }

    i = count - 1;
    while (i > 0) {
        swap_int(&order[0], &order[i]);
        heap_sift_down_order_by_group(list, order, i, 0);
        --i;
    }
}

static void collect_group_report_tokens(const VhCandidateList *list,
                                        const int *order,
                                        int run_start,
                                        int run_end,
                                        VhStringPool *special_tokens,
                                        VhStringPool *unknown_tokens)
{
    int k;

    if (list == NULL || order == NULL || special_tokens == NULL || unknown_tokens == NULL ||
        list->parse_ctx == NULL) {
        return;
    }

    for (k = run_start; k < run_end; ++k) {
        const VhCandidate *c = &list->items[order[k]];
        const char *archive_name = candidate_archive_name(list, c);
        VhParsedName parsed;
        int j;
        unsigned long ignore_off;

        if (archive_name[0] == '\0' || !vh_parse_filename(list->parse_ctx, archive_name, &parsed)) {
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

static void print_pool_tokens_or_none(const VhStringPool *pool)
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

static int candidate_token_count(const VhCandidateList *list, const VhCandidate *candidate)
{
    const char *archive_name;
    VhParsedName parsed;

    if (list == NULL || candidate == NULL || list->parse_ctx == NULL) {
        return 0;
    }

    archive_name = candidate_archive_name(list, candidate);
    if (archive_name[0] == '\0') {
        return 0;
    }

    if (!vh_parse_filename(list->parse_ctx, archive_name, &parsed)) {
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
static void print_memory_estimate(const VhCandidateList *list, int precomputed_largest_duplicate_group_size)
{
    (void)precomputed_largest_duplicate_group_size;
    (void)list;
    printf("Memory estimate: disabled in VH_AMIGA_MINIMAL build\n");
}
#else
static void print_memory_estimate(const VhCandidateList *list, int precomputed_largest_duplicate_group_size)
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
        total_filename_chars += strlen(candidate_archive_name(list, &list->items[i]));
        total_tokens += candidate_token_count(list, &list->items[i]);
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

static void print_report(const VhCandidateList *list)
{
    int i;
    int *order;
    int largest_duplicate_group_size;

    if (list == NULL || list->count <= 0) {
        return;
    }

    order = (int *)malloc((size_t)list->count * sizeof(int));
    if (order == NULL) {
        return;
    }

    for (i = 0; i < list->count; ++i) {
        order[i] = i;
    }

    sort_order_by_group(list, order, list->count);
    largest_duplicate_group_size = 1;

    i = 0;
    while (i < list->count) {
        int run_start = i;
        int run_end = i + 1;
        int any_rejected = 0;
        int run_size;

        while (run_end < list->count &&
               is_same_group(list, order[run_start], order[run_end])) {
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
                printf("Group: %s\n", candidate_group_key(list, &list->items[order[run_start]]));
                printf("Selected:\n");
                for (k = run_start; k < run_end; ++k) {
                    const VhCandidate *c = &list->items[order[k]];
                    if (c->selected) {
                        printf("  %s  score=%ld\n", candidate_archive_name(list, c), c->score);
                    }
                }
                if (selected_candidate == NULL) {
                    printf("  none\n");
                }

                printf("Skipped:\n");
                for (k = run_start; k < run_end; ++k) {
                    const VhCandidate *c = &list->items[order[k]];
                    if (!c->selected) {
                        printf("  %s  score=%ld", candidate_archive_name(list, c), c->score);
                        if (c->rejected) {
                            const char *code_text = reject_code_text(c->reject_code);
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
                    collect_group_report_tokens(list, order, run_start, run_end, &special_tokens, &unknown_tokens);
                    print_pool_tokens_or_none(&special_tokens);
                } else {
                    printf("none\n");
                }

                printf("Unknown tokens:\n  ");
                if (have_group_tokens) {
                    print_pool_tokens_or_none(&unknown_tokens);
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

    free(order);
    print_memory_estimate(list, largest_duplicate_group_size);
}

static void print_memtrace_summary(const char *mode_name,
                                   int candidate_count,
                                   int selected_count,
                                   int group_count,
                                   int duplicate_group_count,
                                   int largest_duplicate_group_size,
                                   unsigned long current_before_cleanup,
                                   const VhMemtrackStats *final_stats,
                                   const VhMemtrackReport *report)
{
    unsigned short i;
    const char *mode_text;

    if (final_stats == NULL || report == NULL) {
        return;
    }

    mode_text = (mode_name != NULL) ? mode_name : "unknown";

    fprintf(stdout, "Memory trace:\n");
    fprintf(stdout, "  mode: %s\n", mode_text);
    fprintf(stdout, "  candidate_count: %d\n", candidate_count);
    fprintf(stdout, "  selected_count: %d\n", selected_count);
    fprintf(stdout, "  group_count: %d\n", group_count);
    fprintf(stdout, "  duplicate_group_count: %d\n", duplicate_group_count);
    fprintf(stdout, "  largest_duplicate_group_size: %d\n", largest_duplicate_group_size);
    fprintf(stdout, "  actual_peak_heap_bytes: %lu\n", final_stats->actual_peak_heap_bytes);
    fprintf(stdout, "  actual_current_heap_bytes: %lu\n", current_before_cleanup);
    fprintf(stdout, "  actual_current_heap_bytes_after_cleanup: %lu\n", final_stats->actual_current_heap_bytes);
    fprintf(stdout, "  allocation_count: %lu\n", final_stats->allocation_count);
    fprintf(stdout, "  free_count: %lu\n", final_stats->free_count);
    fprintf(stdout, "  realloc_count: %lu\n", final_stats->realloc_count);
    fprintf(stdout, "  largest_single_allocation_bytes: %lu\n", final_stats->largest_single_allocation_bytes);
    fprintf(stdout, "  largest_single_allocation_tag: %s\n", report->largest_single_allocation_tag);
    fprintf(stdout, "  largest_single_allocation_operation: %s\n", report->largest_single_allocation_operation);
    fprintf(stdout, "  largest_single_allocation_is_payload_only: yes\n");
    fprintf(stdout, "  failed_allocation_count: %lu\n", final_stats->failed_allocation_count);
    fprintf(stdout, "  all_tracked_allocations_freed: %s\n", vh_memtrack_all_allocations_freed() ? "yes" : "no");

    if (report->tag_count > 0U) {
        fprintf(stdout, "  allocations_by_tag:\n");
        for (i = 0; i < report->tag_count; ++i) {
            const VhMemtrackTagSnapshot *tag = &report->tags[i];
            if ((tag->malloc_count + tag->realloc_count) == 0UL) {
                continue;
            }
            fprintf(stdout,
                    "    %s: malloc=%lu realloc=%lu peak_bytes=%lu current_bytes=%lu\n",
                    tag->name,
                    tag->malloc_count,
                    tag->realloc_count,
                    tag->peak_bytes,
                    tag->current_bytes);
        }
    }
}

static int run_selection(const char *listfile, const char *profile_arg, int report_mode, int memtrace)
{
    VhParseContext parse_ctx;
    VhProfile profile;
    VhCandidateList candidates;
    VhMemtrackStats mem_stats;
    unsigned long mem_before_cleanup;
    int group_count;
    int duplicate_group_count;
    int largest_duplicate_group_size;
    int selected_count;
    int candidate_count;
    const char *mode_name;
    int exit_code;
    char profile_path[VH_PATH_MAX];
    VhMemtrackReport mem_report;

    vh_memtrack_reset();
    mem_before_cleanup = 0UL;
    group_count = 0;
    duplicate_group_count = 0;
    largest_duplicate_group_size = 0;
    selected_count = 0;
    candidate_count = 0;
    mode_name = report_mode ? "report" : "select";
    exit_code = 1;

    if (!build_profile_path(profile_arg, profile_path, sizeof(profile_path))) {
        fprintf(stderr, "Profile path is too long: %s\n", profile_arg);
        return exit_code;
    }

    if (!vh_parse_context_load(&parse_ctx, "data/Defs")) {
        fprintf(stderr, "Failed to load parser CSV definitions from data/Defs\n");
        return exit_code;
    }

    if (!vh_profile_load(&profile, profile_path, &parse_ctx)) {
        vh_parse_context_free(&parse_ctx);
        fprintf(stderr, "Failed to load profile: %s\n", profile_path);
        return exit_code;
    }

    if (!vh_group_load_candidates(&candidates, listfile, &parse_ctx)) {
        vh_profile_free(&profile);
        vh_parse_context_free(&parse_ctx);
        fprintf(stderr, "Failed to load candidate list: %s\n", listfile);
        return exit_code;
    }

    if (!vh_group_select_best(&candidates, &profile)) {
        vh_group_free_candidates(&candidates);
        vh_profile_free(&profile);
        vh_parse_context_free(&parse_ctx);
        fprintf(stderr, "Failed to select candidates\n");
        return exit_code;
    }

    candidate_count = candidates.count;
    vh_group_calculate_stats(&candidates,
                             &group_count,
                             &duplicate_group_count,
                             &largest_duplicate_group_size,
                             &selected_count);

    if (report_mode) {
        print_report(&candidates);
    } else {
        print_selected_candidates(&candidates);
    }

    if (memtrace) {
        vh_memtrack_get_stats(&mem_stats);
        mem_before_cleanup = mem_stats.actual_current_heap_bytes;
    }

    vh_group_free_candidates(&candidates);
    vh_profile_free(&profile);
    vh_parse_context_free(&parse_ctx);

    if (memtrace) {
        vh_memtrack_get_stats(&mem_stats);
        vh_memtrack_get_report(&mem_report);
        print_memtrace_summary(mode_name,
                               candidate_count,
                               selected_count,
                               group_count,
                               duplicate_group_count,
                               largest_duplicate_group_size,
                               mem_before_cleanup,
                               &mem_stats,
                               &mem_report);
    }

    exit_code = 0;
    return exit_code;
}

int main(int argc, char **argv)
{
    int memtrace;

    if (argc <= 1) {
        print_help(argv[0]);
        return 0;
    }

    memtrace = 0;

    if (strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--parse") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s --parse <filename>\n", argv[0]);
            return 1;
        }
        return handle_parse(argv[2]);
    }

    if (strcmp(argv[1], "--resolve") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s --resolve <field> <token>\n", argv[0]);
            print_supported_fields();
            return 1;
        }
        return handle_resolve(argv[2], argv[3]);
    }

    if (strcmp(argv[1], "--select") == 0) {
        int i;

        if (argc < 5 || strcmp(argv[3], "--profile") != 0) {
            fprintf(stderr, "Usage: %s --select <listfile> --profile <name|path>\n", argv[0]);
            return 1;
        }

        for (i = 5; i < argc; ++i) {
            if (strcmp(argv[i], "--memtrace") == 0) {
                memtrace = 1;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                fprintf(stderr, "Usage: %s --select <listfile> --profile <name|path> [--memtrace]\n", argv[0]);
                return 1;
            }
        }

        return run_selection(argv[2], argv[4], 0, memtrace);
    }

    if (strcmp(argv[1], "--report") == 0) {
        int i;

        if (argc < 5 || strcmp(argv[3], "--profile") != 0) {
            fprintf(stderr, "Usage: %s --report <listfile> --profile <name|path>\n", argv[0]);
            return 1;
        }

        for (i = 5; i < argc; ++i) {
            if (strcmp(argv[i], "--memtrace") == 0) {
                memtrace = 1;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                fprintf(stderr, "Usage: %s --report <listfile> --profile <name|path> [--memtrace]\n", argv[0]);
                return 1;
            }
        }

        return run_selection(argv[2], argv[4], 1, memtrace);
    }

    fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    print_help(argv[0]);
    return 1;
}
