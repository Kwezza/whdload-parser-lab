#include "platform.h"
#include "whdtlv/core/tlv_profile.h"
#include "whdtlv/io/writeLog.h"

#include <string.h>

#if PLATFORM_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#elif defined(_WIN32)
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *counter);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *frequency);
#endif

#if TLV_PROFILE_ENABLE

#define AMIGA_TICKS_PER_SECOND 50UL

typedef struct TLV_ProfileCounter {
    const char *name;
    uint64_t total_us;
    uint64_t max_us;
    uint32_t call_count;
} TLV_ProfileCounter;

#if !PLATFORM_AMIGA && defined(_WIN32)
static int64_t g_tlv_profile_frequency;
static bool g_tlv_profile_frequency_ready = false;

static bool tlv_profile_ensure_frequency(void)
{
    if (!g_tlv_profile_frequency_ready) {
        if (!QueryPerformanceFrequency((long long *)&g_tlv_profile_frequency)) {
            return false;
        }
        g_tlv_profile_frequency_ready = true;
    }

    return true;
}
#endif

static TLV_ProfileCounter g_tlv_profile_counters[TLV_PROFILE_SECTION_COUNT] = {
    {"session_init", 0, 0, 0},
    {"batch_total", 0, 0, 0},
    {"record_init", 0, 0, 0},
    {"process_filename", 0, 0, 0},
    {"sanitize", 0, 0, 0},
    {"prescan", 0, 0, 0},
    {"prescan_join", 0, 0, 0},
    {"prescan_lookup", 0, 0, 0},
    {"prescan_rebuild", 0, 0, 0},
    {"tokenize", 0, 0, 0},
    {"token_loop", 0, 0, 0},
    {"token_checks", 0, 0, 0},
    {"pack_field_match", 0, 0, 0},
    {"unknown_token", 0, 0, 0},
    {"csv_lookup", 0, 0, 0},
    {"csv_lookup_loaded", 0, 0, 0},
    {"tlv_add_entry", 0, 0, 0},
    {"aggregate_merge", 0, 0, 0}
};

static uint64_t tlv_profile_elapsed_microseconds(const TLV_ProfileStamp *start,
                                                 const TLV_ProfileStamp *end)
{
#if PLATFORM_AMIGA
    unsigned long start_ticks;
    unsigned long end_ticks;
    unsigned long elapsed_ticks;

    start_ticks = ((unsigned long)start->amiga_stamp.ds_Minute * 60UL * AMIGA_TICKS_PER_SECOND) +
                  (unsigned long)start->amiga_stamp.ds_Tick;
    end_ticks = ((unsigned long)end->amiga_stamp.ds_Minute * 60UL * AMIGA_TICKS_PER_SECOND) +
                (unsigned long)end->amiga_stamp.ds_Tick;

    if (end->amiga_stamp.ds_Days > start->amiga_stamp.ds_Days) {
        end_ticks += (unsigned long)(end->amiga_stamp.ds_Days - start->amiga_stamp.ds_Days) *
                     24UL * 60UL * 60UL * AMIGA_TICKS_PER_SECOND;
    }

    if (end_ticks < start_ticks) {
        return 0;
    }

    elapsed_ticks = end_ticks - start_ticks;
    return ((uint64_t)elapsed_ticks * 1000000ULL) / (uint64_t)AMIGA_TICKS_PER_SECOND;
#elif defined(_WIN32)
    int64_t elapsed_ticks;

    if (!tlv_profile_ensure_frequency()) {
        return 0;
    }

    if (end->host_ticks < start->host_ticks) {
        return 0;
    }

    elapsed_ticks = (int64_t)(end->host_ticks - start->host_ticks);
    return ((uint64_t)elapsed_ticks * 1000000ULL) /
           (uint64_t)g_tlv_profile_frequency;
#else
    if (end->host_microseconds < start->host_microseconds) {
        return 0;
    }

    return end->host_microseconds - start->host_microseconds;
#endif
}

static void tlv_profile_write_summary_line(FILE *stream,
                                           const TLV_ProfileCounter *counter,
                                           uint64_t batch_total_us)
{
    uint64_t average_us;
    uint64_t share_tenths;
    unsigned long total_ms_whole;
    unsigned long total_ms_frac;
    unsigned long avg_ms_whole;
    unsigned long avg_ms_frac;
    unsigned long max_ms_whole;
    unsigned long max_ms_frac;

    if (!stream || !counter || counter->call_count == 0) {
        return;
    }

    average_us = counter->total_us / (uint64_t)counter->call_count;
    share_tenths = 0;
    if (batch_total_us > 0) {
        share_tenths = (counter->total_us * 1000ULL) / batch_total_us;
    }

    total_ms_whole = (unsigned long)(counter->total_us / 1000ULL);
    total_ms_frac = (unsigned long)(counter->total_us % 1000ULL);
    avg_ms_whole = (unsigned long)(average_us / 1000ULL);
    avg_ms_frac = (unsigned long)(average_us % 1000ULL);
    max_ms_whole = (unsigned long)(counter->max_us / 1000ULL);
    max_ms_frac = (unsigned long)(counter->max_us % 1000ULL);

    fprintf(stream,
            "%-18s calls=%-6lu total=%8lu.%03lu ms avg=%8lu.%03lu ms max=%8lu.%03lu ms share=%3lu.%01lu%%\n",
            counter->name,
            (unsigned long)counter->call_count,
            total_ms_whole,
            total_ms_frac,
            avg_ms_whole,
            avg_ms_frac,
            max_ms_whole,
            max_ms_frac,
            (unsigned long)(share_tenths / 10ULL),
            (unsigned long)(share_tenths % 10ULL));
}

void tlv_profile_reset(void)
{
    uint32_t index;

    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        g_tlv_profile_counters[index].total_us = 0;
        g_tlv_profile_counters[index].max_us = 0;
        g_tlv_profile_counters[index].call_count = 0;
    }
}

void tlv_profile_section_start(TLV_ProfileStamp *stamp)
{
    if (!stamp) {
        return;
    }

#if PLATFORM_AMIGA
    DateStamp(&stamp->amiga_stamp);
#elif defined(_WIN32)
    if (tlv_profile_ensure_frequency()) {
        QueryPerformanceCounter((long long *)&stamp->host_ticks);
    } else {
        stamp->host_ticks = 0;
    }
#else
    stamp->host_microseconds = (uint64_t)(((double)clock() * 1000000.0) / (double)CLOCKS_PER_SEC);
#endif
}

void tlv_profile_section_stop(TLV_ProfileSection section, const TLV_ProfileStamp *stamp)
{
    TLV_ProfileStamp end_stamp;
    uint64_t elapsed_us;

    if (!stamp || section >= TLV_PROFILE_SECTION_COUNT) {
        return;
    }

    tlv_profile_section_start(&end_stamp);
    elapsed_us = tlv_profile_elapsed_microseconds(stamp, &end_stamp);

    g_tlv_profile_counters[section].total_us += elapsed_us;
    g_tlv_profile_counters[section].call_count++;
    if (elapsed_us > g_tlv_profile_counters[section].max_us) {
        g_tlv_profile_counters[section].max_us = elapsed_us;
    }
}

void tlv_profile_print_summary(FILE *stream)
{
    uint32_t index;
    uint64_t batch_total_us;
    bool printed_any;

    if (!stream) {
        return;
    }

    batch_total_us = g_tlv_profile_counters[TLV_PROFILE_SECTION_BATCH_TOTAL].total_us;
    printed_any = false;

    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        if (g_tlv_profile_counters[index].call_count > 0) {
            printed_any = true;
            break;
        }
    }

    if (!printed_any) {
        return;
    }

    fprintf(stream, "TLV pipeline profile (save excluded):\n");
    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        tlv_profile_write_summary_line(stream, &g_tlv_profile_counters[index], batch_total_us);
    }
}

void tlv_profile_log_summary(void)
{
    uint32_t index;
    uint64_t batch_total_us;
    uint64_t average_us;
    uint64_t share_tenths;
    unsigned long total_ms_whole;
    unsigned long total_ms_frac;
    unsigned long avg_ms_whole;
    unsigned long avg_ms_frac;
    unsigned long max_ms_whole;
    unsigned long max_ms_frac;

    if (!whdtlv_log_is_enabled()) {
        return;
    }

    batch_total_us = g_tlv_profile_counters[TLV_PROFILE_SECTION_BATCH_TOTAL].total_us;
    whdtlv_log_append("TLV pipeline profile (save excluded):");

    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        const TLV_ProfileCounter *counter = &g_tlv_profile_counters[index];
        if (counter->call_count == 0) {
            continue;
        }

        average_us = counter->total_us / (uint64_t)counter->call_count;
        share_tenths = 0;
        if (batch_total_us > 0) {
            share_tenths = (counter->total_us * 1000ULL) / batch_total_us;
        }

        total_ms_whole = (unsigned long)(counter->total_us / 1000ULL);
        total_ms_frac = (unsigned long)(counter->total_us % 1000ULL);
        avg_ms_whole = (unsigned long)(average_us / 1000ULL);
        avg_ms_frac = (unsigned long)(average_us % 1000ULL);
        max_ms_whole = (unsigned long)(counter->max_us / 1000ULL);
        max_ms_frac = (unsigned long)(counter->max_us % 1000ULL);

        whdtlv_log_append("%s calls=%lu total=%lu.%03lu ms avg=%lu.%03lu ms max=%lu.%03lu ms share=%lu.%lu%%",
                      counter->name,
                      (unsigned long)counter->call_count,
                      total_ms_whole,
                      total_ms_frac,
                      avg_ms_whole,
                      avg_ms_frac,
                      max_ms_whole,
                      max_ms_frac,
                      (unsigned long)(share_tenths / 10ULL),
                      (unsigned long)(share_tenths % 10ULL));
    }
}

bool tlv_profile_is_enabled(void)
{
    return true;
}

#else

void tlv_profile_reset(void)
{
}

void tlv_profile_section_start(TLV_ProfileStamp *unused_stamp)
{
    if (unused_stamp) {
    }
}

void tlv_profile_section_stop(TLV_ProfileSection unused_section, const TLV_ProfileStamp *unused_stamp)
{
    if (unused_section || unused_stamp) {
    }
}

void tlv_profile_print_summary(FILE *unused_stream)
{
    if (unused_stream) {
    }
}

void tlv_profile_log_summary(void)
{
}

bool tlv_profile_is_enabled(void)
{
    return false;
}

#endif

/* End of Text */
