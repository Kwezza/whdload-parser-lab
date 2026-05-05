#ifndef TLV_FILENAME_TLV_PROFILE_H
#define TLV_FILENAME_TLV_PROFILE_H

#include <platform.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#if PLATFORM_AMIGA
#include <dos/dos.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TLV_PROFILE_ENABLE
#define TLV_PROFILE_ENABLE 0
#endif

typedef enum TLV_ProfileSection {
    TLV_PROFILE_SECTION_SESSION_INIT = 0,
    TLV_PROFILE_SECTION_BATCH_TOTAL,
    TLV_PROFILE_SECTION_RECORD_INIT,
    TLV_PROFILE_SECTION_PROCESS_FILENAME,
    TLV_PROFILE_SECTION_SANITIZE,
    TLV_PROFILE_SECTION_PRESCAN,
    TLV_PROFILE_SECTION_PRESCAN_JOIN_CONSTRUCTION,
    TLV_PROFILE_SECTION_PRESCAN_CSV_LOOKUP,
    TLV_PROFILE_SECTION_PRESCAN_REBUILD_STRIP,
    TLV_PROFILE_SECTION_TOKENIZE,
    TLV_PROFILE_SECTION_TOKEN_LOOP,
    TLV_PROFILE_SECTION_TOKEN_CHECKS,
    TLV_PROFILE_SECTION_PACK_FIELD_CSV_MATCH,
    TLV_PROFILE_SECTION_UNKNOWN_TOKEN,
    TLV_PROFILE_SECTION_CSV_LOOKUP,
    TLV_PROFILE_SECTION_CSV_LOOKUP_LOADED,
    TLV_PROFILE_SECTION_TLV_ADD_ENTRY,
    TLV_PROFILE_SECTION_AGGREGATE_MERGE,
    TLV_PROFILE_SECTION_COUNT
} TLV_ProfileSection;

typedef struct TLV_ProfileStamp {
#if PLATFORM_AMIGA
    struct DateStamp amiga_stamp;
#elif defined(_WIN32)
    int64_t host_ticks;
#else
    uint64_t host_microseconds;
#endif
} TLV_ProfileStamp;

void tlv_profile_reset(void);
void tlv_profile_section_start(TLV_ProfileStamp *stamp);
void tlv_profile_section_stop(TLV_ProfileSection section, const TLV_ProfileStamp *stamp);
void tlv_profile_print_summary(FILE *stream);
void tlv_profile_log_summary(void);
bool tlv_profile_is_enabled(void);

#if TLV_PROFILE_ENABLE
#define TLV_PROFILE_SCOPE(name) TLV_ProfileStamp name
#define TLV_PROFILE_START(name) tlv_profile_section_start(&(name))
#define TLV_PROFILE_END(section, name) tlv_profile_section_stop((section), &(name))
#else
#define TLV_PROFILE_SCOPE(name)
#define TLV_PROFILE_START(name) do { } while (0)
#define TLV_PROFILE_END(section, name) do { } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TLV_FILENAME_TLV_PROFILE_H */

/* End of Text */
