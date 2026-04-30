#ifndef VH_MEMTRACK_H
#define VH_MEMTRACK_H

#include <stddef.h>
#include <stdio.h>

typedef struct VhMemtrackStats {
    unsigned long actual_current_heap_bytes;
    unsigned long actual_peak_heap_bytes;
    unsigned long allocation_count;
    unsigned long free_count;
    unsigned long realloc_count;
    unsigned long largest_single_allocation_bytes;
    unsigned long failed_allocation_count;
} VhMemtrackStats;

void vh_memtrack_reset(void);
void vh_memtrack_get_stats(VhMemtrackStats *out_stats);
int vh_memtrack_all_allocations_freed(void);

void *vh_malloc_tag(size_t size, const char *tag);
void *vh_realloc_tag(void *ptr, size_t size, const char *tag);
void *vh_malloc(size_t size);
void *vh_realloc(void *ptr, size_t size);
void vh_free(void *ptr);

void vh_memtrack_print_summary(FILE *out,
                               const char *mode_name,
                               int candidate_count,
                               int selected_count,
                               int group_count,
                               int duplicate_group_count,
                               int largest_duplicate_group_size,
                               unsigned long current_before_cleanup,
                               const VhMemtrackStats *final_stats);

#endif