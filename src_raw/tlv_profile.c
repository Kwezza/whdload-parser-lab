#include <platform.h>
#include <tlv_filename/tlv_profile.h>
#include <io/writeLog.h>

#include <string.h>

#if PLATFORM_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#endif

#if TLV_PROFILE_ENABLE

#define AMIGA_TICKS_PER_SECOND 50UL

typedef struct TLV_ProfileCounter {
    const char *name;
    unsigned long total_ms;
    unsigned long max_ms;
    uint32_t call_count;
} TLV_ProfileCounter;

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
    {"tlv_add_entry", 0, 0, 0},
    {"aggregate_merge", 0, 0, 0}
};

static unsigned long tlv_profile_elapsed_milliseconds(const TLV_ProfileStamp *start,
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
    return (elapsed_ticks * 1000UL) / AMIGA_TICKS_PER_SECOND;
#else
    clock_t elapsed_ticks;

    if (end->host_ticks < start->host_ticks) {
        return 0;
    }

    elapsed_ticks = end->host_ticks - start->host_ticks;
    return (unsigned long)((elapsed_ticks * 1000UL) / CLOCKS_PER_SEC);
#endif
}

static void tlv_profile_write_summary_line(FILE *stream,
                                           const TLV_ProfileCounter *counter,
                                           unsigned long batch_total_ms)
{
    unsigned long average_ms;
    unsigned long share_tenths;

    if (!stream || !counter || counter->call_count == 0) {
        return;
    }

    average_ms = counter->total_ms / (unsigned long)counter->call_count;
    share_tenths = 0;
    if (batch_total_ms > 0) {
        share_tenths = (counter->total_ms * 1000UL) / batch_total_ms;
    }

    fprintf(stream,
            "%-18s calls=%-6lu total=%-8lu ms avg=%-6lu ms max=%-6lu ms share=%3lu.%01lu%%\n",
            counter->name,
            (unsigned long)counter->call_count,
            counter->total_ms,
            average_ms,
            counter->max_ms,
            share_tenths / 10UL,
            share_tenths % 10UL);
}

void tlv_profile_reset(void)
{
    uint32_t index;

    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        g_tlv_profile_counters[index].total_ms = 0;
        g_tlv_profile_counters[index].max_ms = 0;
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
#else
    stamp->host_ticks = clock();
#endif
}

void tlv_profile_section_stop(TLV_ProfileSection section, const TLV_ProfileStamp *stamp)
{
    TLV_ProfileStamp end_stamp;
    unsigned long elapsed_ms;

    if (!stamp || section >= TLV_PROFILE_SECTION_COUNT) {
        return;
    }

    tlv_profile_section_start(&end_stamp);
    elapsed_ms = tlv_profile_elapsed_milliseconds(stamp, &end_stamp);

    g_tlv_profile_counters[section].total_ms += elapsed_ms;
    g_tlv_profile_counters[section].call_count++;
    if (elapsed_ms > g_tlv_profile_counters[section].max_ms) {
        g_tlv_profile_counters[section].max_ms = elapsed_ms;
    }
}

void tlv_profile_print_summary(FILE *stream)
{
    uint32_t index;
    unsigned long batch_total_ms;
    bool printed_any;

    if (!stream) {
        return;
    }

    batch_total_ms = g_tlv_profile_counters[TLV_PROFILE_SECTION_BATCH_TOTAL].total_ms;
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
        tlv_profile_write_summary_line(stream, &g_tlv_profile_counters[index], batch_total_ms);
    }
}

void tlv_profile_log_summary(void)
{
    uint32_t index;
    unsigned long batch_total_ms;
    unsigned long average_ms;
    unsigned long share_tenths;

    if (!is_logging_enabled()) {
        return;
    }

    batch_total_ms = g_tlv_profile_counters[TLV_PROFILE_SECTION_BATCH_TOTAL].total_ms;
    append_to_log("TLV pipeline profile (save excluded):");

    for (index = 0; index < (uint32_t)TLV_PROFILE_SECTION_COUNT; index++) {
        const TLV_ProfileCounter *counter = &g_tlv_profile_counters[index];
        if (counter->call_count == 0) {
            continue;
        }

        average_ms = counter->total_ms / (unsigned long)counter->call_count;
        share_tenths = 0;
        if (batch_total_ms > 0) {
            share_tenths = (counter->total_ms * 1000UL) / batch_total_ms;
        }

        append_to_log("%s calls=%lu total=%lu ms avg=%lu ms max=%lu ms share=%lu.%lu%%",
                      counter->name,
                      (unsigned long)counter->call_count,
                      counter->total_ms,
                      average_ms,
                      counter->max_ms,
                      share_tenths / 10UL,
                      share_tenths % 10UL);
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
