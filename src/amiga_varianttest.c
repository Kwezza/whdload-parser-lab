#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include "vh_group.h"
#include "vh_memtrack.h"
#include "vh_parse.h"
#include "vh_profile.h"

#define VT_LABEL "varianttest"
#define VT_VERSION "Phase15"

#define VT_PATH_MAX 512
#define VT_PREVIEW_COUNT 10

#define VT_DEFAULT_DAT "temp/Dat files/Games(2026-04-17).txt"
#define VT_DEFAULT_DAT_ALT1 "Bin/Amiga/temp/Dat files/Games(2026-04-17).txt"
#define VT_DEFAULT_DAT_ALT2 "PROGDIR:temp/Dat files/Games(2026-04-17).txt"

#define VT_RESULT_FILE "PROGDIR:varianttest_result.txt"
#define VT_RESULT_FILE_FALLBACK "varianttest_result.txt"
#define VT_SELECTED_FILE "PROGDIR:varianttest_selected.txt"
#define VT_SELECTED_FILE_FALLBACK "varianttest_selected.txt"

static int vt_file_exists(const char *path)
{
    FILE *fp;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    fclose(fp);
    return 1;
}

static FILE *vt_open_with_fallback(const char *primary, const char *fallback, char *used_path, size_t used_path_size)
{
    FILE *fp;

    fp = fopen(primary, "w");
    if (fp != NULL) {
        if (used_path != NULL && used_path_size > 0U) {
            strncpy(used_path, primary, used_path_size - 1U);
            used_path[used_path_size - 1U] = '\0';
        }
        return fp;
    }

    fp = fopen(fallback, "w");
    if (fp != NULL && used_path != NULL && used_path_size > 0U) {
        strncpy(used_path, fallback, used_path_size - 1U);
        used_path[used_path_size - 1U] = '\0';
    }

    return fp;
}

static int vt_has_pathish_chars(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    if (strchr(text, '/') != NULL) {
        return 1;
    }
    if (strchr(text, '\\') != NULL) {
        return 1;
    }
    if (strchr(text, ':') != NULL) {
        return 1;
    }
    if (strchr(text, '.') != NULL) {
        return 1;
    }
    return 0;
}

static int vt_build_profile_candidate(const char *profile_name, const char *base, char *out_path, size_t out_size)
{
    size_t base_len;
    size_t name_len;

    if (profile_name == NULL || base == NULL || out_path == NULL || out_size == 0U) {
        return 0;
    }

    base_len = strlen(base);
    name_len = strlen(profile_name);

    if (base_len + 1U + name_len + 8U >= out_size) {
        return 0;
    }

    strcpy(out_path, base);
    strcat(out_path, "/");
    strcat(out_path, profile_name);
    strcat(out_path, ".profile");

    return 1;
}

static int vt_resolve_defs_dir(char *out_defs_dir, size_t out_defs_size)
{
    const char *candidates[4];
    int i;

    candidates[0] = "data/Defs";
    candidates[1] = "tools/variant_harness/data/Defs";
    candidates[2] = "PROGDIR:data/Defs";
    candidates[3] = "PROGDIR:tools/variant_harness/data/Defs";

    for (i = 0; i < 4; ++i) {
        char probe[VT_PATH_MAX];
        size_t cand_len;

        cand_len = strlen(candidates[i]);
        if (cand_len + 13U >= sizeof(probe)) {
            continue;
        }

        strcpy(probe, candidates[i]);
        strcat(probe, "/Language.csv");

        if (vt_file_exists(probe)) {
            strncpy(out_defs_dir, candidates[i], out_defs_size - 1U);
            out_defs_dir[out_defs_size - 1U] = '\0';
            return 1;
        }
    }

    return 0;
}

static int vt_resolve_profile_path(const char *profile_arg, char *out_profile, size_t out_profile_size)
{
    const char *profile_name;
    char candidate[VT_PATH_MAX];
    const char *bases[4];
    int i;

    if (out_profile == NULL || out_profile_size == 0U) {
        return 0;
    }

    profile_name = (profile_arg == NULL || profile_arg[0] == '\0') ? "default" : profile_arg;

    if (vt_has_pathish_chars(profile_name)) {
        if (!vt_file_exists(profile_name)) {
            return 0;
        }
        strncpy(out_profile, profile_name, out_profile_size - 1U);
        out_profile[out_profile_size - 1U] = '\0';
        return 1;
    }

    bases[0] = "data/Profiles";
    bases[1] = "tools/variant_harness/data/Profiles";
    bases[2] = "PROGDIR:data/Profiles";
    bases[3] = "PROGDIR:tools/variant_harness/data/Profiles";

    for (i = 0; i < 4; ++i) {
        if (!vt_build_profile_candidate(profile_name, bases[i], candidate, sizeof(candidate))) {
            continue;
        }
        if (vt_file_exists(candidate)) {
            strncpy(out_profile, candidate, out_profile_size - 1U);
            out_profile[out_profile_size - 1U] = '\0';
            return 1;
        }
    }

    return 0;
}

static int vt_resolve_dat_path(const char *dat_arg, char *out_dat, size_t out_dat_size)
{
    const char *candidates[3];
    int i;

    if (out_dat == NULL || out_dat_size == 0U) {
        return 0;
    }

    if (dat_arg != NULL && dat_arg[0] != '\0') {
        if (!vt_file_exists(dat_arg)) {
            return 0;
        }
        strncpy(out_dat, dat_arg, out_dat_size - 1U);
        out_dat[out_dat_size - 1U] = '\0';
        return 1;
    }

    candidates[0] = VT_DEFAULT_DAT;
    candidates[1] = VT_DEFAULT_DAT_ALT1;
    candidates[2] = VT_DEFAULT_DAT_ALT2;

    for (i = 0; i < 3; ++i) {
        if (vt_file_exists(candidates[i])) {
            strncpy(out_dat, candidates[i], out_dat_size - 1U);
            out_dat[out_dat_size - 1U] = '\0';
            return 1;
        }
    }

    return 0;
}

static void vt_emit(FILE *out, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    if (out != NULL) {
        va_start(args, fmt);
        vfprintf(out, fmt, args);
        va_end(args);
    }
}

static void vt_print_usage(const char *argv0)
{
    printf("%s (%s)\n", VT_LABEL, VT_VERSION);
    printf("Usage: %s [DATFILE] [PROFILE]\n", argv0);
    printf("Examples:\n");
    printf("  %s\n", argv0);
    printf("  %s \"Bin/Amiga/temp/Dat files/Games(2026-04-17).txt\"\n", argv0);
    printf("  %s \"Bin/Amiga/temp/Dat files/Games(2026-04-17).txt\" default\n", argv0);
}

static int vt_write_selected_files(const VhCandidateList *candidates,
                                   FILE *summary_out,
                                   const char *selected_primary,
                                   const char *selected_fallback,
                                   char *selected_path_used,
                                   size_t selected_path_used_size,
                                   char *error_msg,
                                   size_t error_msg_size)
{
    FILE *selected_out;
    int i;
    int preview_left;

    selected_out = vt_open_with_fallback(selected_primary,
                                         selected_fallback,
                                         selected_path_used,
                                         selected_path_used_size);
    if (selected_out == NULL) {
        if (error_msg != NULL && error_msg_size > 0U) {
            strncpy(error_msg, "failed to write selected output file", error_msg_size - 1U);
            error_msg[error_msg_size - 1U] = '\0';
        }
        return 0;
    }

    preview_left = VT_PREVIEW_COUNT;
    vt_emit(summary_out, "selected_preview_first_%d:\n", VT_PREVIEW_COUNT);

    for (i = 0; i < candidates->count; ++i) {
        const VhCandidate *c;
        const char *name;

        c = &candidates->items[i];
        if (!c->selected) {
            continue;
        }

        name = vh_string_pool_get(&candidates->strings, c->archive_name_off);
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        fprintf(selected_out, "%s\n", name);

        if (preview_left > 0) {
            vt_emit(summary_out, "  %s\n", name);
            --preview_left;
        }
    }

    fclose(selected_out);
    return 1;
}

int main(int argc, char **argv)
{
    const char *dat_arg;
    const char *profile_arg;
    char dat_path[VT_PATH_MAX];
    char profile_path[VT_PATH_MAX];
    char defs_dir[VT_PATH_MAX];
    char summary_path_used[VT_PATH_MAX];
    char selected_path_used[VT_PATH_MAX];
    FILE *summary_out;
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
    clock_t start_clock;
    clock_t end_clock;
    long elapsed_ms;
    int have_timing;
    int ok;
    int exit_code;
    char error_msg[256];

    dat_arg = NULL;
    profile_arg = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "HELP") == 0 || strcmp(argv[1], "?") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            vt_print_usage(argv[0]);
            return 0;
        }
        dat_arg = argv[1];
    }

    if (argc > 2) {
        profile_arg = argv[2];
    }

    summary_out = vt_open_with_fallback(VT_RESULT_FILE, VT_RESULT_FILE_FALLBACK, summary_path_used, sizeof(summary_path_used));
    if (summary_out == NULL) {
        fprintf(stderr, "%s: failed to open summary output file\n", VT_LABEL);
        return 1;
    }

    selected_path_used[0] = '\0';

    if (!vt_resolve_dat_path(dat_arg, dat_path, sizeof(dat_path))) {
        vt_emit(summary_out, "%s (%s)\n", VT_LABEL, VT_VERSION);
        vt_emit(summary_out, "result: failure\n");
        vt_emit(summary_out, "error: DAT file not found\n");
        vt_emit(summary_out, "summary_file: %s\n", summary_path_used);
        fclose(summary_out);
        return 1;
    }

    if (!vt_resolve_profile_path(profile_arg, profile_path, sizeof(profile_path))) {
        vt_emit(summary_out, "%s (%s)\n", VT_LABEL, VT_VERSION);
        vt_emit(summary_out, "dat_file: %s\n", dat_path);
        vt_emit(summary_out, "result: failure\n");
        vt_emit(summary_out, "error: profile not found\n");
        vt_emit(summary_out, "summary_file: %s\n", summary_path_used);
        fclose(summary_out);
        return 1;
    }

    if (!vt_resolve_defs_dir(defs_dir, sizeof(defs_dir))) {
        vt_emit(summary_out, "%s (%s)\n", VT_LABEL, VT_VERSION);
        vt_emit(summary_out, "dat_file: %s\n", dat_path);
        vt_emit(summary_out, "profile: %s\n", profile_path);
        vt_emit(summary_out, "result: failure\n");
        vt_emit(summary_out, "error: parser defs directory not found\n");
        vt_emit(summary_out, "summary_file: %s\n", summary_path_used);
        fclose(summary_out);
        return 1;
    }

    vh_memtrack_reset();
    memset(&parse_ctx, 0, sizeof(parse_ctx));
    memset(&profile, 0, sizeof(profile));
    memset(&candidates, 0, sizeof(candidates));
    memset(&mem_stats, 0, sizeof(mem_stats));
    mem_before_cleanup = 0UL;
    group_count = 0;
    duplicate_group_count = 0;
    largest_duplicate_group_size = 0;
    selected_count = 0;
    candidate_count = 0;
    elapsed_ms = -1L;
    have_timing = 0;
    exit_code = 1;
    error_msg[0] = '\0';

    start_clock = clock();

    ok = vh_parse_context_load(&parse_ctx, defs_dir);
    if (!ok) {
        strncpy(error_msg, "failed to load parser definitions", sizeof(error_msg) - 1U);
        error_msg[sizeof(error_msg) - 1U] = '\0';
        goto done;
    }

    ok = vh_profile_load(&profile, profile_path, &parse_ctx);
    if (!ok) {
        strncpy(error_msg, "failed to load profile", sizeof(error_msg) - 1U);
        error_msg[sizeof(error_msg) - 1U] = '\0';
        goto done;
    }

    ok = vh_group_load_candidates(&candidates, dat_path, &parse_ctx);
    if (!ok) {
        strncpy(error_msg, "failed to load candidate list", sizeof(error_msg) - 1U);
        error_msg[sizeof(error_msg) - 1U] = '\0';
        goto done;
    }

    ok = vh_group_select_best(&candidates, &profile);
    if (!ok) {
        strncpy(error_msg, "failed during selection", sizeof(error_msg) - 1U);
        error_msg[sizeof(error_msg) - 1U] = '\0';
        goto done;
    }

    vh_group_calculate_stats(&candidates,
                             &group_count,
                             &duplicate_group_count,
                             &largest_duplicate_group_size,
                             &selected_count);
    candidate_count = candidates.count;

    ok = vt_write_selected_files(&candidates,
                                 summary_out,
                                 VT_SELECTED_FILE,
                                 VT_SELECTED_FILE_FALLBACK,
                                 selected_path_used,
                                 sizeof(selected_path_used),
                                 error_msg,
                                 sizeof(error_msg));
    if (!ok) {
        goto done;
    }

    exit_code = 0;

done:
    end_clock = clock();
    if (start_clock != (clock_t)-1 && end_clock != (clock_t)-1) {
        if (CLOCKS_PER_SEC > 0) {
            elapsed_ms = (long)(((end_clock - start_clock) * 1000L) / CLOCKS_PER_SEC);
            have_timing = 1;
        }
    }

    vh_memtrack_get_stats(&mem_stats);
    mem_before_cleanup = mem_stats.actual_current_heap_bytes;

    vh_group_free_candidates(&candidates);
    vh_profile_free(&profile);
    vh_parse_context_free(&parse_ctx);

    vh_memtrack_get_stats(&mem_stats);

    vt_emit(summary_out, "%s (%s)\n", VT_LABEL, VT_VERSION);
    vt_emit(summary_out, "dat_file: %s\n", dat_path);
    vt_emit(summary_out, "profile: %s\n", profile_path);
    vt_emit(summary_out, "defs_dir: %s\n", defs_dir);
    vt_emit(summary_out, "candidate_count: %d\n", candidate_count);
    vt_emit(summary_out, "selected_count: %d\n", selected_count);
    vt_emit(summary_out, "group_count: %d\n", group_count);
    vt_emit(summary_out, "duplicate_group_count: %d\n", duplicate_group_count);
    vt_emit(summary_out, "largest_duplicate_group_size: %d\n", largest_duplicate_group_size);

    if (have_timing) {
        vt_emit(summary_out, "elapsed_ms: %ld\n", elapsed_ms);
    } else {
        vt_emit(summary_out, "elapsed: timing not available in this build\n");
    }

    vt_emit(summary_out, "actual_peak_heap_bytes: %lu\n", mem_stats.actual_peak_heap_bytes);
    vt_emit(summary_out, "actual_current_heap_bytes: %lu\n", mem_before_cleanup);
    vt_emit(summary_out, "actual_current_heap_bytes_after_cleanup: %lu\n", mem_stats.actual_current_heap_bytes);
    vt_emit(summary_out, "largest_single_allocation_bytes: %lu\n", mem_stats.largest_single_allocation_bytes);
    vt_emit(summary_out, "allocation_count: %lu\n", mem_stats.allocation_count);
    vt_emit(summary_out, "free_count: %lu\n", mem_stats.free_count);
    vt_emit(summary_out, "realloc_count: %lu\n", mem_stats.realloc_count);
    vt_emit(summary_out, "failed_allocation_count: %lu\n", mem_stats.failed_allocation_count);
    vt_emit(summary_out, "all_tracked_allocations_freed: %s\n", vh_memtrack_all_allocations_freed() ? "yes" : "no");

    if (selected_path_used[0] == '\0') {
        strncpy(selected_path_used, VT_SELECTED_FILE_FALLBACK, sizeof(selected_path_used) - 1U);
        selected_path_used[sizeof(selected_path_used) - 1U] = '\0';
    }

    vt_emit(summary_out, "summary_file: %s\n", summary_path_used);
    vt_emit(summary_out, "selected_file: %s\n", selected_path_used);

    if (exit_code == 0) {
        vt_emit(summary_out, "result: success\n");
    } else {
        vt_emit(summary_out, "result: failure\n");
        if (error_msg[0] != '\0') {
            vt_emit(summary_out, "error: %s\n", error_msg);
        }
    }

    fclose(summary_out);
    return exit_code;
}
