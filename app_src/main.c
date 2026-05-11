#include <platform.h>
#include <platform/platform_io.h>
#include <platform/platform_string.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/tlv_profile.h>
#include <tlv_filename/csv_cache.h>
#include <tlv_filename/filename_processor.h>
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
/* VBCC stack override: helps avoid late-exit crashes from stack exhaustion. */
unsigned long __stack = 131072UL;
#elif defined(_WIN32)
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *counter);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *frequency);
#endif

#define DEFAULT_DAT_COUNT        5
#define DEFAULT_CSV_DIR         "assets_raw/defs"

static const char * const DEFAULT_DAT_PATHS[DEFAULT_DAT_COUNT] = {
    "assets_raw/Dats/DemB(2026-04-20).dat",
    "assets_raw/Dats/Demo(2026-03-23).dat",
    "assets_raw/Dats/GamB(2026-04-26).dat",
    "assets_raw/Dats/Game(2026-04-17).dat",
    "assets_raw/Dats/Mags(2025-07-24).dat"
};

static const char * const DEFAULT_OUTPUT_PATHS[DEFAULT_DAT_COUNT] = {
    "output/DemB(2026-04-20).tlv",
    "output/Demo(2026-03-23).tlv",
    "output/GamB(2026-04-26).tlv",
    "output/Game(2026-04-17).tlv",
    "output/Mags(2025-07-24).tlv"
};
#define DEFAULT_PACK_TYPES_PATH "assets_raw/prefs/pack_types.ini"
#define DEFAULT_SUMMARY_LOG_FILE "benchmark-summary.txt"
#define AMIGA_TICKS_PER_SECOND  50UL

typedef struct BenchmarkStamp {
#if PLATFORM_AMIGA
    struct DateStamp amiga_stamp;
#elif defined(_WIN32)
    int64_t host_ticks;
#else
    uint64_t host_microseconds;
#endif
} BenchmarkStamp;

#if !PLATFORM_AMIGA && defined(_WIN32)
static int64_t g_benchmark_frequency;
static bool g_benchmark_frequency_ready = false;

static bool benchmark_ensure_frequency(void)
{
    if (!g_benchmark_frequency_ready) {
        if (!QueryPerformanceFrequency((long long *)&g_benchmark_frequency)) {
            return false;
        }
        g_benchmark_frequency_ready = true;
    }

    return true;
}
#endif

/**
 * @brief Capture a timestamp for benchmark measurements.
 */
static BenchmarkStamp benchmark_now(void)
{
    BenchmarkStamp stamp;

#if PLATFORM_AMIGA
    DateStamp(&stamp.amiga_stamp);
#elif defined(_WIN32)
    if (benchmark_ensure_frequency()) {
        QueryPerformanceCounter((long long *)&stamp.host_ticks);
    } else {
        stamp.host_ticks = 0;
    }
#else
    stamp.host_microseconds = (uint64_t)(((double)clock() * 1000000.0) / (double)CLOCKS_PER_SEC);
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
#if defined(_WIN32)
    int64_t elapsed_ticks;

    if (!benchmark_ensure_frequency()) {
        return 0;
    }

    if (end.host_ticks < start.host_ticks) {
        return 0;
    }

    elapsed_ticks = (int64_t)(end.host_ticks - start.host_ticks);
    return (unsigned long)(((uint64_t)elapsed_ticks * 1000ULL) /
                           (uint64_t)g_benchmark_frequency);
#else
    if (end.host_microseconds < start.host_microseconds) {
        return 0;
    }

    return (unsigned long)((end.host_microseconds - start.host_microseconds) / 1000ULL);
#endif
#endif
}

#ifdef PLATFORM_AMIGA
static void print_amiga_stage(const char *stage)
{
    if (!stage || stage[0] == '\0') {
        return;
    }

    printf("[stage] %s\n", stage);
    fflush(stdout);
}
#endif

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
        csv_cache_print_stats(summary_file);
        filename_processor_print_pack_field_stats(summary_file);
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
 * @brief Write a uint32 in big-endian (Motorola) byte order into dst[0..3].
 */
static void encode_u32_be(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)((val >> 24) & 0xffu);
    dst[1] = (uint8_t)((val >> 16) & 0xffu);
    dst[2] = (uint8_t)((val >>  8) & 0xffu);
    dst[3] = (uint8_t)( val        & 0xffu);
}

/**
 * @brief Encode archive_info 8-byte payload: size_kib (BE) then crc32 (BE).
 *
 * size_kib = (size_bytes + 1023) / 1024   (rounded-up KiB, uint32)
 * crc32    = raw CRC-32 value from the DAT crc= attribute
 */
static void encode_archive_info(uint8_t buf[8],
                                uint32_t size_bytes,
                                uint32_t crc32_val)
{
    uint32_t size_kib;
    if (size_bytes == 0) {
        size_kib = 0;
    } else {
        size_kib = (size_bytes + 1023u) / 1024u;
    }
    encode_u32_be(buf,     size_kib);
    encode_u32_be(buf + 4, crc32_val);
}

/**
 * @brief Print usage for the standalone converter.
 */
static void print_usage(const char *program_name)
{
    printf("Usage: %s [--max-log] [--summary-log summary_path] [dat_path output_path [csv_dir pack_types_ini]]\n", program_name);
    printf("Defaults:\n");
    printf("  dat_path       = (processes all %d default DAT files)\n", DEFAULT_DAT_COUNT);
    printf("  output_path    = output/<DatName>(<date>).tlv\n");
    printf("  csv_dir        = %s\n", DEFAULT_CSV_DIR);
    printf("  pack_types_ini = %s\n", DEFAULT_PACK_TYPES_PATH);
    printf("  summary_log    = executable-folder/%s\n", DEFAULT_SUMMARY_LOG_FILE);
    printf("Options:\n");
    printf("  --max-log      Enable verbose logfile creation and logfile writes\n");
    printf("  --summary-log  Override the default summary log path\n");
}

/**
 * @brief Process a single DAT file through the full TLV pipeline.
 *
 * Assumes tlv_session_init() has already been called. Handles all per-DAT
 * allocations and cleanup internally.
 *
 * @return true on success, false on any error.
 */
static bool process_dat_file(const char *dat_path,
                             const char *output_path,
                             const char *csv_dir,
                             const char *pack_types_path,
                             const PackType *pack_types,
                             size_t pack_count,
                             const char *summary_log_path,
                             bool summary_log_is_default)
{
    DatRomEntry *dat_entries;
    const char **name_ptrs;
    size_t filename_count;
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
    char resolved_summary_log_path[512];
    size_t i;
    TLV_PROFILE_SCOPE(aggregate_merge_profile_stamp);

    dat_entries = NULL;
    name_ptrs = NULL;
    filename_count = 0;
    records = NULL;
    field_registry = NULL;
    output_file = NULL;
    build_elapsed_ms = 0;
    save_elapsed_ms = 0;
    success = false;
    memset(&aggregate, 0, sizeof(aggregate));
    copy_path_string(resolved_summary_log_path, sizeof(resolved_summary_log_path), summary_log_path);

    whdtlv_log_append("Standalone DAT-to-TLV run starting for '%s'", dat_path);
    whdtlv_log_append("Stage: parsing DAT filenames");
#ifdef PLATFORM_AMIGA
    print_amiga_stage("parsing DAT filenames");
#endif

    filename_count = parse_dat_entries_minimal(dat_path, &dat_entries);
    if (filename_count == 0 || !dat_entries) {
        whdtlv_log_append("ERROR: no DAT filenames extracted from '%s'", dat_path);
        fprintf(stderr, "No DAT filenames extracted from %s\n", dat_path);
        goto cleanup;
    }
    whdtlv_log_append("Stage complete: parsed %lu DAT filenames", (unsigned long)filename_count);

    name_ptrs = (const char **)whd_malloc(filename_count * sizeof(const char *));
    if (!name_ptrs) {
        fprintf(stderr, "Failed to allocate name pointer array\n");
        goto cleanup;
    }
    for (i = 0; i < filename_count; i++) {
        name_ptrs[i] = dat_entries[i].name;
    }

    pack_index = find_pack_index_for_dat(pack_types, pack_count, dat_path);

    records = (TLV_Record *)whd_malloc(filename_count * sizeof(TLV_Record));
    if (!records) {
        fprintf(stderr, "Failed to allocate record array\n");
        goto cleanup;
    }
    memset(records, 0, filename_count * sizeof(TLV_Record));

    build_start = benchmark_now();
#ifdef PLATFORM_AMIGA
    print_amiga_stage("processing filename batch");
#endif
    if (!tlv_session_process_batch(name_ptrs,
                                   (uint32_t)filename_count,
                                   (uint32_t)pack_index,
                                   records,
                                   &summary)) {
        fprintf(stderr, "Failed to process DAT batch\n");
        goto cleanup;
    }

#ifdef PLATFORM_AMIGA
    print_amiga_stage("injecting archive info");
#endif

    /* Build registry now so we can resolve the archive_info field ID for injection */
    field_registry = field_registry_alloc();
    if (!field_registry || !build_field_registry_from_ini(field_registry, pack_types_path)) {
        fprintf(stderr, "Failed to rebuild field registry for output\n");
        goto cleanup;
    }

    /* Inject archive_info (size_kib + crc32, big-endian) into each per-file record */
    {
        uint8_t archive_info_id;
        uint8_t buf[8];

        archive_info_id = field_registry_get_id(field_registry, "archive_info");
        if (archive_info_id != 0) {
            for (i = 0; i < filename_count; i++) {
                if (records[i].entry_count == 0) {
                    continue;
                }
                encode_archive_info(buf,
                                    dat_entries[i].size_bytes,
                                    dat_entries[i].crc32);
                if (!tlv_record_add_entry(&records[i], archive_info_id, buf, 8)) {
                    fprintf(stderr, "WARNING: failed to add archive_info for record %lu\n",
                            (unsigned long)i);
                }
            }
        } else {
            fprintf(stderr, "WARNING: archive_info field not found in registry\n");
        }
    }

    /* Inject group_id fields into per-file records.
     * Must happen before aggregation so group_id entries are merged in. */
    if (!tlv_session_inject_group_ids(records, (uint32_t)filename_count)) {
        fprintf(stderr, "WARNING: failed to inject group_id fields\n");
        whdtlv_log_append("WARNING: failed to inject group_id fields");
    }

#ifdef PLATFORM_AMIGA
    print_amiga_stage("aggregating TLV records");
#endif
    if (!tlv_record_init(&aggregate)) {
        fprintf(stderr, "Failed to initialize aggregate TLV record\n");
        goto cleanup;
    }

    {
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

    ensure_parent_directory_exists(output_path);
    save_start = benchmark_now();
#ifdef PLATFORM_AMIGA
    print_amiga_stage("writing TLV output");
#endif
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

#ifdef PLATFORM_AMIGA
    print_amiga_stage("writing summary log");
#endif
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
        whdtlv_log_append("WARNING: failed to append summary log %s", summary_log_path);
    } else if (strcmp(resolved_summary_log_path, summary_log_path) != 0) {
        printf("Summary log fallback used: %s\n", resolved_summary_log_path);
    }

    if (tlv_profile_is_enabled()) {
#ifdef PLATFORM_AMIGA
        print_amiga_stage("printing profile summary");
#endif
        tlv_profile_print_summary(stdout);
        csv_cache_print_stats(stdout);
        filename_processor_print_pack_field_stats(stdout);
        tlv_profile_log_summary();
    }

cleanup:
#ifdef PLATFORM_AMIGA
    print_amiga_stage(success ? "final cleanup" : "cleanup after failure");
#endif
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
    if (name_ptrs) {
        whd_free(name_ptrs);
    }
    if (dat_entries) {
        free_dat_entries_minimal(dat_entries, filename_count);
    }
    return success;
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
    bool logging_requested;
    bool summary_log_is_default;
    PackType *pack_types;
    size_t pack_count;
    bool all_success;
    int positional_argc;
    const char *positional_args[4];
    int arg_index;
    int dat_index;

    dat_path = NULL;
    output_path = NULL;
    csv_dir = DEFAULT_CSV_DIR;
    pack_types_path = DEFAULT_PACK_TYPES_PATH;
    logging_requested = false;
    pack_types = NULL;
    pack_count = 0;
    all_success = false;
    summary_log_is_default = true;
    positional_argc = 0;
    build_default_summary_log_path(argv[0], default_summary_log_path, sizeof(default_summary_log_path));
    summary_log_path = default_summary_log_path;

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

    whdtlv_log_set_enabled(logging_requested);
    whdtlv_log_init();
    tlv_profile_reset();
    whdtlv_log_append("WARNING: set a high Amiga stack manually before running (example: STACK 100000) to avoid crashes");

    whdtlv_log_append("Stage: loading pack types");
#ifdef PLATFORM_AMIGA
    print_amiga_stage("loading pack types");
#endif
    pack_types = whdtlv_load_pack_types(pack_types_path, &pack_count);
    if (!pack_types || pack_count == 0) {
        whdtlv_log_append("ERROR: failed to load pack types from '%s'", pack_types_path);
        fprintf(stderr, "Failed to load pack types from %s\n", pack_types_path);
        goto cleanup;
    }
    whdtlv_log_append("Stage complete: loaded %lu pack types", (unsigned long)pack_count);

    whdtlv_log_append("Stage: initializing TLV session");
#ifdef PLATFORM_AMIGA
    print_amiga_stage("initializing TLV session");
#endif
    if (!tlv_session_init(csv_dir, pack_types_path)) {
        whdtlv_log_append("ERROR: failed to initialize TLV session");
        fprintf(stderr, "Failed to initialize TLV session\n");
        goto cleanup;
    }
    whdtlv_log_append("Stage complete: TLV session initialized");

    all_success = true;
    if (positional_argc >= 2) {
        if (!process_dat_file(dat_path,
                              output_path,
                              csv_dir,
                              pack_types_path,
                              pack_types,
                              pack_count,
                              summary_log_path,
                              summary_log_is_default)) {
            all_success = false;
        }
    } else {
        for (dat_index = 0; dat_index < DEFAULT_DAT_COUNT; dat_index++) {
            printf("\n--- Processing %s ---\n", DEFAULT_DAT_PATHS[dat_index]);
            if (!process_dat_file(DEFAULT_DAT_PATHS[dat_index],
                                  DEFAULT_OUTPUT_PATHS[dat_index],
                                  csv_dir,
                                  pack_types_path,
                                  pack_types,
                                  pack_count,
                                  summary_log_path,
                                  summary_log_is_default)) {
                fprintf(stderr, "Failed: %s\n", DEFAULT_DAT_PATHS[dat_index]);
                all_success = false;
            }
        }
    }

cleanup:
#ifdef PLATFORM_AMIGA
    print_amiga_stage(all_success ? "final cleanup" : "cleanup after failure");
#endif
    tlv_session_finalize();
    if (pack_types) {
        whdtlv_free_pack_types(pack_types, pack_count);
    }
    whdtlv_prettify_shutdown();

    return all_success ? 0 : 1;
}
