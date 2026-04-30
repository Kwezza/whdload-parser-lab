#ifndef VH_MEMTRACK_H
#define VH_MEMTRACK_H

#include <stddef.h>

#define VH_MEMTRACK_MAX_TAGS 32
#define VH_MEMTRACK_MAX_TAG_NAME 32

typedef struct VhMemtrackStats {
    unsigned long actual_current_heap_bytes;
    unsigned long actual_peak_heap_bytes;
    unsigned long allocation_count;
    unsigned long free_count;
    unsigned long realloc_count;
    unsigned long largest_single_allocation_bytes;
    unsigned long failed_allocation_count;
} VhMemtrackStats;

typedef struct VhMemtrackTagSnapshot {
    char name[VH_MEMTRACK_MAX_TAG_NAME];
    unsigned long malloc_count;
    unsigned long realloc_count;
    unsigned long current_bytes;
    unsigned long peak_bytes;
} VhMemtrackTagSnapshot;

typedef struct VhMemtrackReport {
    char largest_single_allocation_tag[VH_MEMTRACK_MAX_TAG_NAME];
    char largest_single_allocation_operation[16];
    VhMemtrackTagSnapshot tags[VH_MEMTRACK_MAX_TAGS];
    unsigned short tag_count;
} VhMemtrackReport;

void vh_memtrack_reset(void);
void vh_memtrack_get_stats(VhMemtrackStats *out_stats);
int vh_memtrack_all_allocations_freed(void);

void *vh_malloc_tag(size_t size, const char *tag);
void *vh_realloc_tag(void *ptr, size_t size, const char *tag);
void *vh_malloc(size_t size);
void *vh_realloc(void *ptr, size_t size);
void vh_free(void *ptr);
void vh_memtrack_get_report(VhMemtrackReport *out_report);

#endif