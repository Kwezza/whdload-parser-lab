#include <platform.h>
#include <platform/platform_io.h>
#include <platform/platform_string.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/tlv_profile.h>
#include <tlv_filename/field_registry.h>
#include <io/pack_types_loader.h>
#include <io/writeLog.h>
#include <utils/prettify.h>
#include "dat_parser_minimal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#if PLATFORM_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#endif

#define DEFAULT_DAT_PATH        "assets_raw/Games(19-05-2025).dat"
#define DEFAULT_OUTPUT_PATH     "output/Games(19-05-2025).tlv"
#define DEFAULT_CSV_DIR         "assets_raw/defs"
#define DEFAULT_PACK_TYPES_PATH "assets_raw/prefs/pack_types.ini"
#define DEFAULT_SUMMARY_LOG_FILE "benchmark-summary.txt"
#define AMIGA_TICKS_PER_SECOND  50UL

typedef struct BenchmarkStamp {
#if PLATFORM_AMIGA
    struct DateStamp amiga_stamp;
#else
    clock_t host_ticks;
#endif
} BenchmarkStamp;

/**
 * @brief Capture a timestamp for benchmark measurements.
 */
static BenchmarkStamp benchmark_now(void)
{
    BenchmarkStamp stamp;

#if PLATFORM_AMIGA
    DateStamp(&stamp.amiga_stamp);
#else
    stamp.host_ticks = clock();
#endif

    return stamp;
}

/**
 * @brief Convert a benchmark span to milliseconds.
 */
static unsigned long benchmark_elapsed_milliseconds(BenchmarkStamp start,
                                                    BenchmarkStamp end)
{
#if PLATFORM_AMIGA
    unsigned long start_ticks;
    unsigned long end_ticks;
    unsigned long elapsed_ticks;

    start_ticks = ((unsigned long)start.amiga_stamp.ds_Minute * 60UL * AMIGA_TICKS_PER_SECOND) +
                  (unsigned long)start.amiga_stamp.ds_Tick;
    end_ticks = ((unsigned long)end.amiga_stamp.ds_Minute * 60UL * AMIGA_TICKS_PER_SECOND) +
                (unsigned long)end.amiga_stamp.ds_Tick;

    if (end.amiga_stamp.ds_Days > start.amiga_stamp.ds_Days) {
        end_ticks += (unsigned long)(end.amiga_stamp.ds_Days - start.amiga_stamp.ds_Days) *
                     24UL * 60UL * 60UL * AMIGA_TICKS_PER_SECOND;
    }

    if (end_ticks < start_ticks) {
        return 0;
    }

    elapsed_ticks = end_ticks - start_ticks;
    return (elapsed_ticks * 1000UL) / AMIGA_TICKS_PER_SECOND;
#else
    clock_t elapsed_ticks;

    if (end.host_ticks < start.host_ticks) {
        return 0;
    }

    elapsed_ticks = end.host_ticks - start.host_ticks;
    return (unsigned long)((elapsed_ticks * 1000UL) / CLOCKS_PER_SEC);
#endif
}

/**
 * @brief Ensure the parent directory for a path exists.
 */
static void ensure_parent_directory_exists(const char *path)
{
    const char *slash;
    char directory[512];
    size_t length;

    if (!path) {
        return;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        slash = strrchr(path, '\\');
    }
    if (!slash) {
        return;
    }

    length = (size_t)(slash - path);
    if (length == 0 || length >= sizeof(directory)) {
        return;
    }

    memcpy(directory, path, length);
    directory[length] = '\0';
    (void)whd_mkdir(directory);
}

/**
 * @brief Copy a path string into a fixed-size output buffer.
 */
static void copy_path_string(char *destination, size_t destination_capacity, const char *source)
{
    size_t length;

    if (!destination || destination_capacity == 0) {
        return;
    }

    if (!source) {
        destination[0] = '\0';
        return;
    }

    length = strlen(source);
    if (length >= destination_capacity) {
        length = destination_capacity - 1;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';
}

/**
 * @brief Build the default summary log path in the executable folder.
 */
static void build_default_summary_log_path(const char *program_name,
                                           char *output_path,
                                           size_t output_capacity)
{
#if PLATFORM_AMIGA
    const char *default_path = "PROGDIR:" DEFAULT_SUMMARY_LOG_FILE;
    size_t length;

    if (!output_path || output_capacity == 0) {
        return;
    }

    length = strlen(default_path);
    if (length >= output_capacity) {
        length = output_capacity - 1;
    }

    memcpy(output_path, default_path, length);
    output_path[length] = '\0';
#else
    const char *slash;
    size_t length;

    if (!output_path || output_capacity == 0) {
        return;
    }

    if (!program_name || program_name[0] == '\0') {
        program_name = DEFAULT_SUMMARY_LOG_FILE;
    }

    slash = strrchr(program_name, '/');
    if (!slash) {
        slash = strrchr(program_name, '\\');
    }

    if (!slash) {
        length = strlen(DEFAULT_SUMMARY_LOG_FILE);
        if (length >= output_capacity) {
            length = output_capacity - 1;
        }
        memcpy(output_path, DEFAULT_SUMMARY_LOG_FILE, length);
        output_path[length] = '\0';
        return;
    }

    length = (size_t)(slash - program_name) + 1;
    if (length >= output_capacity) {
        length = output_capacity - 1;
    }

    memcpy(output_path, program_name, length);
    output_path[length] = '\0';

    if (strlen(output_path) + strlen(DEFAULT_SUMMARY_LOG_FILE) < output_capacity) {
        strcat(output_path, DEFAULT_SUMMARY_LOG_FILE);
    }
#endif
}

/**
 * @brief Print the standalone conversion summary to a stream.
 */
static void write_summary_stream(FILE *stream,
                                 const char *dat_path,
                                 const char *output_path,
                                 const char *csv_dir,
                                 const char *pack_types_path,
                                 size_t filename_count,
                                 const ProcessingSummary *summary,
                                 const TLV_Record *aggregate,
                                 unsigned long build_elapsed_ms,
                                 unsigned long save_elapsed_ms)
{
    if (!stream || !dat_path || !output_path || !csv_dir || !pack_types_path ||
        !summary || !aggregate) {
        return;
    }

    fprintf(stream, "DAT input:    %s\n", dat_path);
    fprintf(stream, "Output TLV:   %s\n", output_path);
    fprintf(stream, "CSV folder:   %s\n", csv_dir);
    fprintf(stream, "Pack types:   %s\n", pack_types_path);
    fprintf(stream, "DAT entries:  %lu\n", (unsigned long)filename_count);
    fprintf(stream, "Processed:    %lu\n", (unsigned long)summary->total_processed);
    fprintf(stream, "Successful:   %lu\n", (unsigned long)summary->successful_count);
    fprintf(stream, "Errors:       %lu\n", (unsigned long)summary->error_count);
    fprintf(stream, "TLV entries:  %lu\n", (unsigned long)aggregate->entry_count);
    fprintf(stream, "TLV build time: %lu ms\n", build_elapsed_ms);
    fprintf(stream, "TLV save time: %lu ms\n", save_elapsed_ms);
}

/**
 * @brief Append the standalone conversion summary to a summary log file.
 */
static bool append_summary_log_file(const char *summary_log_path,
                                    const char *dat_path,
                                    const char *output_path,
                                    const char *csv_dir,
                                    const char *pack_types_path,
                                    size_t filename_count,
                                    const ProcessingSummary *summary,
                                    const TLV_Record *aggregate,
                                    unsigned long build_elapsed_ms,
                                    unsigned long save_elapsed_ms)
{
    FILE *summary_file;

    if (!summary_log_path || summary_log_path[0] == '\0') {
        return true;
    }

    ensure_parent_directory_exists(summary_log_path);
    summary_file = whd_fopen(summary_log_path, "a");
    if (!summary_file) {
        return false;
    }

    write_summary_stream(summary_file,
                         dat_path,
                         output_path,
                         csv_dir,
                         pack_types_path,
                         filename_count,
                         summary,
                         aggregate,
                         build_elapsed_ms,
                         save_elapsed_ms);
    if (tlv_profile_is_enabled()) {
        fprintf(summary_file, "\n");
        tlv_profile_print_summary(summary_file);
    }
    fprintf(summary_file, "\n");
    whd_fclose(summary_file);

    return true;
}

/**
 * @brief Free an array of TLV records.
 */
static void free_record_array(TLV_Record *records, size_t count)
{
    size_t i;

    if (!records) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (records[i].entries) {
            tlv_record_free(&records[i]);
        }
    }

    whd_free(records);
}

/**
 * @brief Merge one TLV record into an aggregate TLV record.
 */
static bool merge_record_into_aggregate(TLV_Record *aggregate, const TLV_Record *source)
{
    uint32_t i;

    if (!aggregate || !source) {
        return false;
    }

    for (i = 0; i < source->entry_count; i++) {
        const TLV_Entry *entry = &source->entries[i];
        if (!tlv_record_add_entry(aggregate, entry->field_id, entry->value, entry->length)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Append the standalone conversion summary to the preferred summary log path.
 */
static bool append_summary_log(const char *summary_log_path,
                               bool allow_default_fallback,
                               char *resolved_summary_log_path,
                               size_t resolved_summary_log_path_capacity,
                               const char *dat_path,
                               const char *output_path,
                               const char *csv_dir,
                               const char *pack_types_path,
                               size_t filename_count,
                               const ProcessingSummary *summary,
                               const TLV_Record *aggregate,
                               unsigned long build_elapsed_ms,
                               unsigned long save_elapsed_ms)
{
    if (append_summary_log_file(summary_log_path,
                                dat_path,
                                output_path,
                                csv_dir,
                                pack_types_path,
                                filename_count,
                                summary,
                                aggregate,
                                build_elapsed_ms,
                                save_elapsed_ms)) {
        copy_path_string(resolved_summary_log_path,
                         resolved_summary_log_path_capacity,
                         summary_log_path);
        return true;
    }

    if (allow_default_fallback) {
        const char *fallback_path = DEFAULT_SUMMARY_LOG_FILE;
        if (append_summary_log_file(fallback_path,
                                    dat_path,
                                    output_path,
                                    csv_dir,
                                    pack_types_path,
                                    filename_count,
                                    summary,
                                    aggregate,
                                    build_elapsed_ms,
                                    save_elapsed_ms)) {
            copy_path_string(resolved_summary_log_path,
                             resolved_summary_log_path_capacity,
                             fallback_path);
            return true;
        }
    }

    copy_path_string(resolved_summary_log_path,
                     resolved_summary_log_path_capacity,
                     summary_log_path);
    return false;
}

/**
 * @brief Extract the DAT stem from a DAT path for pack matching.
 */
static void get_dat_stem(const char *dat_path, char *out_stem, size_t out_capacity)
{
    const char *base_name;
    const char *end;
    size_t length;

    if (!dat_path || !out_stem || out_capacity == 0) {
        return;
    }

    base_name = strrchr(dat_path, '/');
    if (!base_name) {
        base_name = strrchr(dat_path, '\\');
    }
    base_name = base_name ? base_name + 1 : dat_path;

    end = strchr(base_name, '(');
    if (!end) {
        end = strrchr(base_name, '.');
    }
    if (!end) {
        end = base_name + strlen(base_name);
    }

    length = (size_t)(end - base_name);
    if (length >= out_capacity) {
        length = out_capacity - 1;
    }

    memcpy(out_stem, base_name, length);
    out_stem[length] = '\0';
}

/**
 * @brief Find the pack-type array index matching the DAT stem.
 */
static size_t find_pack_index_for_dat(const PackType *pack_types,
                                      size_t pack_count,
                                      const char *dat_path)
{
    char dat_stem[64];
    size_t index;

    get_dat_stem(dat_path, dat_stem, sizeof(dat_stem));

    for (index = 0; index < pack_count; index++) {
        if (pack_types[index].dat_name &&
            whd_strcasecmp(pack_types[index].dat_name, dat_stem) == 0) {
            return index;
        }
    }

    return 0;
}

/**
 * @brief Print usage for the standalone converter.
 */
static void print_usage(const char *program_name)
{
    printf("Usage: %s [--max-log] [--summary-log summary_path] [dat_path output_path [csv_dir pack_types_ini]]\n", program_name);
    printf("Defaults:\n");
    printf("  dat_path       = %s\n", DEFAULT_DAT_PATH);
    printf("  output_path    = %s\n", DEFAULT_OUTPUT_PATH);
    printf("  csv_dir        = %s\n", DEFAULT_CSV_DIR);
    printf("  pack_types_ini = %s\n", DEFAULT_PACK_TYPES_PATH);
    printf("  summary_log    = executable-folder/%s\n", DEFAULT_SUMMARY_LOG_FILE);
    printf("Options:\n");
    printf("  --max-log      Enable verbose logfile creation and logfile writes\n");
    printf("  --summary-log  Override the default summary log path\n");
}

/**
 * @brief Standalone host entry point for DAT-to-TLV conversion.
 */
int main(int argc, char **argv)
{
    const char *dat_path;
    const char *output_path;
    const char *csv_dir;
    const char *pack_types_path;
    const char *summary_log_path;
    char default_summary_log_path[512];
    char resolved_summary_log_path[512];
    bool logging_requested;
    bool summary_log_is_default;
    char **filenames;
    size_t filename_count;
    PackType *pack_types;
    size_t pack_count;
    size_t pack_index;
    TLV_Record *records;
    TLV_Record aggregate;
    ProcessingSummary summary;
    FieldRegistry *field_registry;
    FILE *output_file;
    BenchmarkStamp build_start;
    BenchmarkStamp build_end;
    BenchmarkStamp save_start;
    BenchmarkStamp save_end;
    unsigned long build_elapsed_ms;
    unsigned long save_elapsed_ms;
    bool success;
    int positional_argc;
    const char *positional_args[4];
    int arg_index;
    TLV_PROFILE_SCOPE(aggregate_merge_profile_stamp);

    dat_path = DEFAULT_DAT_PATH;
    output_path = DEFAULT_OUTPUT_PATH;
    csv_dir = DEFAULT_CSV_DIR;
    pack_types_path = DEFAULT_PACK_TYPES_PATH;
    logging_requested = false;
    filenames = NULL;
    filename_count = 0;
    pack_types = NULL;
    pack_count = 0;
    records = NULL;
    field_registry = NULL;
    output_file = NULL;
    build_elapsed_ms = 0;
    save_elapsed_ms = 0;
    build_default_summary_log_path(argv[0], default_summary_log_path, sizeof(default_summary_log_path));
    summary_log_path = default_summary_log_path;
    copy_path_string(resolved_summary_log_path, sizeof(resolved_summary_log_path), default_summary_log_path);
    summary_log_is_default = true;
    success = false;
    positional_argc = 0;
    memset(&aggregate, 0, sizeof(aggregate));

    for (arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], "--max-log") == 0) {
            logging_requested = true;
            continue;
        }

        if (strcmp(argv[arg_index], "--summary-log") == 0) {
            arg_index++;
            if (arg_index >= argc) {
                print_usage(argv[0]);
                return 1;
            }

            summary_log_path = argv[arg_index];
            summary_log_is_default = false;
            continue;
        }

        if (strcmp(argv[arg_index], "--help") == 0 ||
            strcmp(argv[arg_index], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (positional_argc >= 4) {
            print_usage(argv[0]);
            return 1;
        }

        positional_args[positional_argc] = argv[arg_index];
        positional_argc++;
    }

    if (positional_argc != 0 && positional_argc != 2 && positional_argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    if (positional_argc >= 2) {
        dat_path = positional_args[0];
        output_path = positional_args[1];
    }
    if (positional_argc == 4) {
        csv_dir = positional_args[2];
        pack_types_path = positional_args[3];
    }

    set_logging_enabled(logging_requested);
    initialize_logfile();
    tlv_profile_reset();
    append_to_log("WARNING: set a high Amiga stack manually before running (example: STACK 100000) to avoid crashes");
    append_to_log("Standalone DAT-to-TLV run starting for '%s'", dat_path);
    append_to_log("Stage: parsing DAT filenames");

    filename_count = parse_dat_filenames_minimal(dat_path, &filenames);
    if (filename_count == 0 || !filenames) {
        append_to_log("ERROR: no DAT filenames extracted from '%s'", dat_path);
        fprintf(stderr, "No DAT filenames extracted from %s\n", dat_path);
        goto cleanup;
    }
    append_to_log("Stage complete: parsed %lu DAT filenames", (unsigned long)filename_count);

    append_to_log("Stage: loading pack types");
    pack_types = load_pack_types(pack_types_path, &pack_count);
    if (!pack_types || pack_count == 0) {
        append_to_log("ERROR: failed to load pack types from '%s'", pack_types_path);
        fprintf(stderr, "Failed to load pack types from %s\n", pack_types_path);
        goto cleanup;
    }
    append_to_log("Stage complete: loaded %lu pack types", (unsigned long)pack_count);

    pack_index = find_pack_index_for_dat(pack_types, pack_count, dat_path);

    append_to_log("Stage: initializing TLV session");
    if (!tlv_session_init(csv_dir, pack_types_path)) {
        append_to_log("ERROR: failed to initialize TLV session");
        fprintf(stderr, "Failed to initialize TLV session\n");
        goto cleanup;
    }
    append_to_log("Stage complete: TLV session initialized");

    records = (TLV_Record *)whd_malloc(filename_count * sizeof(TLV_Record));
    if (!records) {
        fprintf(stderr, "Failed to allocate record array\n");
        goto cleanup;
    }
    memset(records, 0, filename_count * sizeof(TLV_Record));

    build_start = benchmark_now();
    if (!tlv_session_process_batch((const char **)filenames,
                                   (uint32_t)filename_count,
                                   (uint32_t)pack_index,
                                   records,
                                   &summary)) {
        fprintf(stderr, "Failed to process DAT batch\n");
        goto cleanup;
    }

    if (!tlv_record_init(&aggregate)) {
        fprintf(stderr, "Failed to initialize aggregate TLV record\n");
        goto cleanup;
    }

    {
        size_t i;
        TLV_PROFILE_START(aggregate_merge_profile_stamp);
        for (i = 0; i < filename_count; i++) {
            if (records[i].entry_count == 0) {
                continue;
            }
            if (!merge_record_into_aggregate(&aggregate, &records[i])) {
                fprintf(stderr, "Failed to aggregate TLV record %lu\n", (unsigned long)i);
                goto cleanup;
            }
        }
        TLV_PROFILE_END(TLV_PROFILE_SECTION_AGGREGATE_MERGE, aggregate_merge_profile_stamp);
    }
    build_end = benchmark_now();
    build_elapsed_ms = benchmark_elapsed_milliseconds(build_start, build_end);

    if (aggregate.entry_count == 0) {
        fprintf(stderr, "No TLV entries were generated\n");
        goto cleanup;
    }

    field_registry = field_registry_alloc();
    if (!field_registry || !build_field_registry_from_ini(field_registry, pack_types_path)) {
        fprintf(stderr, "Failed to rebuild field registry for output\n");
        goto cleanup;
    }

    ensure_parent_directory_exists(output_path);
    save_start = benchmark_now();
    output_file = whd_fopen(output_path, "wb");
    if (!output_file) {
        fprintf(stderr, "Failed to open output file %s\n", output_path);
        goto cleanup;
    }

    if (!tlv_write_record_with_metadata(output_file, &aggregate, field_registry)) {
        fprintf(stderr, "Failed to write TLV output\n");
        goto cleanup;
    }

    whd_fclose(output_file);
    output_file = NULL;
    save_end = benchmark_now();
    save_elapsed_ms = benchmark_elapsed_milliseconds(save_start, save_end);

    success = true;
    write_summary_stream(stdout,
                         dat_path,
                         output_path,
                         csv_dir,
                         pack_types_path,
                         filename_count,
                         &summary,
                         &aggregate,
                         build_elapsed_ms,
                         save_elapsed_ms);
    printf("Summary log:  %s\n", summary_log_path);

    if (!append_summary_log(summary_log_path,
                            summary_log_is_default,
                            resolved_summary_log_path,
                            sizeof(resolved_summary_log_path),
                            dat_path,
                            output_path,
                            csv_dir,
                            pack_types_path,
                            filename_count,
                            &summary,
                            &aggregate,
                            build_elapsed_ms,
                            save_elapsed_ms)) {
        fprintf(stderr, "WARNING: failed to append summary log %s\n", summary_log_path);
        append_to_log("WARNING: failed to append summary log %s", summary_log_path);
    } else if (strcmp(resolved_summary_log_path, summary_log_path) != 0) {
        printf("Summary log fallback used: %s\n", resolved_summary_log_path);
    }

    if (tlv_profile_is_enabled()) {
        tlv_profile_print_summary(stdout);
        tlv_profile_log_summary();
    }

cleanup:
    if (output_file) {
        whd_fclose(output_file);
    }
    if (field_registry) {
        field_registry_free(field_registry);
    }
    if (aggregate.entries) {
        tlv_record_free(&aggregate);
    }
    if (records) {
        free_record_array(records, filename_count);
    }
    tlv_session_finalize();
    if (pack_types) {
        free_pack_types(pack_types, pack_count);
    }
    if (filenames) {
        free_dat_filenames_minimal(filenames, filename_count);
    }
    prettify_shutdown();

    return success ? 0 : 1;
}
