#include "vh_memtrack.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct VhMemHeader {
    unsigned long magic;
    size_t size;
    unsigned short tag_index;
} VhMemHeader;

typedef struct VhTagStats {
    char name[32];
    unsigned long malloc_count;
    unsigned long realloc_count;
    unsigned long current_bytes;
    unsigned long peak_bytes;
} VhTagStats;

#define VH_MEM_MAGIC 0x56484D54UL
#define VH_MEM_MAX_TAGS 32
#define VH_MEM_TAG_UNKNOWN 0

static VhMemtrackStats g_stats;
static VhTagStats g_tag_stats[VH_MEM_MAX_TAGS];
static unsigned short g_tag_count = 0;
static unsigned short g_largest_tag_index = VH_MEM_TAG_UNKNOWN;
static const char *g_largest_operation = "none";

static unsigned long vh_to_ulong(size_t value)
{
    if (value > (size_t)ULONG_MAX) {
        return ULONG_MAX;
    }
    return (unsigned long)value;
}

static int vh_add_overflow(size_t a, size_t b)
{
    return a > ((size_t)-1 - b);
}

static unsigned short vh_find_or_add_tag(const char *tag)
{
    unsigned short i;
    const char *name;

    name = (tag != NULL && tag[0] != '\0') ? tag : "unknown";

    if (g_tag_count == 0U) {
        memset(g_tag_stats, 0, sizeof(g_tag_stats));
        strncpy(g_tag_stats[0].name, "unknown", sizeof(g_tag_stats[0].name) - 1U);
        g_tag_stats[0].name[sizeof(g_tag_stats[0].name) - 1U] = '\0';
        g_tag_count = 1U;
    }

    for (i = 0; i < g_tag_count; ++i) {
        if (strcmp(g_tag_stats[i].name, name) == 0) {
            return i;
        }
    }

    if (g_tag_count >= VH_MEM_MAX_TAGS) {
        return VH_MEM_TAG_UNKNOWN;
    }

    i = g_tag_count;
    strncpy(g_tag_stats[i].name, name, sizeof(g_tag_stats[i].name) - 1U);
    g_tag_stats[i].name[sizeof(g_tag_stats[i].name) - 1U] = '\0';
    g_tag_count++;
    return i;
}

static const char *vh_get_tag_name(unsigned short tag_index)
{
    if (tag_index >= g_tag_count) {
        return "unknown";
    }
    return g_tag_stats[tag_index].name;
}

static void vh_tag_add_bytes(unsigned short tag_index, unsigned long bytes)
{
    VhTagStats *tag;

    if (tag_index >= g_tag_count) {
        tag_index = VH_MEM_TAG_UNKNOWN;
    }

    tag = &g_tag_stats[tag_index];
    tag->current_bytes += bytes;
    if (tag->current_bytes > tag->peak_bytes) {
        tag->peak_bytes = tag->current_bytes;
    }
}

static void vh_tag_sub_bytes(unsigned short tag_index, unsigned long bytes)
{
    VhTagStats *tag;

    if (tag_index >= g_tag_count) {
        tag_index = VH_MEM_TAG_UNKNOWN;
    }

    tag = &g_tag_stats[tag_index];
    if (tag->current_bytes >= bytes) {
        tag->current_bytes -= bytes;
    } else {
        tag->current_bytes = 0UL;
    }
}

static void vh_update_peak(void)
{
    if (g_stats.actual_current_heap_bytes > g_stats.actual_peak_heap_bytes) {
        g_stats.actual_peak_heap_bytes = g_stats.actual_current_heap_bytes;
    }
}

void vh_memtrack_reset(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_tag_stats, 0, sizeof(g_tag_stats));
    strncpy(g_tag_stats[0].name, "unknown", sizeof(g_tag_stats[0].name) - 1U);
    g_tag_stats[0].name[sizeof(g_tag_stats[0].name) - 1U] = '\0';
    g_tag_count = 1U;
    g_largest_tag_index = VH_MEM_TAG_UNKNOWN;
    g_largest_operation = "none";
}

void vh_memtrack_get_stats(VhMemtrackStats *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = g_stats;
    }
}

int vh_memtrack_all_allocations_freed(void)
{
    return g_stats.actual_current_heap_bytes == 0UL;
}

void *vh_malloc_tag(size_t size, const char *tag)
{
    VhMemHeader *header;
    size_t payload_size;
    size_t total_size;
    unsigned long requested;
    unsigned short tag_index;

    payload_size = (size == 0U) ? 1U : size;
    if (vh_add_overflow(payload_size, sizeof(VhMemHeader))) {
        g_stats.failed_allocation_count++;
        return NULL;
    }

    total_size = payload_size + sizeof(VhMemHeader);
    header = (VhMemHeader *)malloc(total_size);
    if (header == NULL) {
        g_stats.failed_allocation_count++;
        return NULL;
    }

    header->magic = VH_MEM_MAGIC;
    header->size = payload_size;
    tag_index = vh_find_or_add_tag(tag);
    header->tag_index = tag_index;

    g_stats.allocation_count++;
    requested = vh_to_ulong(payload_size);
    g_stats.actual_current_heap_bytes += requested;
    g_tag_stats[tag_index].malloc_count++;
    vh_tag_add_bytes(tag_index, requested);
    if (requested > g_stats.largest_single_allocation_bytes) {
        g_stats.largest_single_allocation_bytes = requested;
        g_largest_tag_index = tag_index;
        g_largest_operation = "malloc";
    }
    vh_update_peak();

    return (void *)(header + 1);
}

void *vh_malloc(size_t size)
{
    return vh_malloc_tag(size, "unknown");
}

void *vh_realloc_tag(void *ptr, size_t size, const char *tag)
{
    VhMemHeader *old_header;
    VhMemHeader *new_header;
    size_t payload_size;
    size_t total_size;
    unsigned long old_size;
    unsigned long new_size;
    unsigned short old_tag_index;
    unsigned short new_tag_index;

    g_stats.realloc_count++;

    if (ptr == NULL) {
        return vh_malloc_tag(size, tag);
    }

    if (size == 0U) {
        vh_free(ptr);
        return NULL;
    }

    old_header = ((VhMemHeader *)ptr) - 1;
    if (old_header->magic != VH_MEM_MAGIC) {
        g_stats.failed_allocation_count++;
        return NULL;
    }

    old_tag_index = old_header->tag_index;
    if (old_tag_index >= g_tag_count) {
        old_tag_index = VH_MEM_TAG_UNKNOWN;
    }

    payload_size = size;
    if (vh_add_overflow(payload_size, sizeof(VhMemHeader))) {
        g_stats.failed_allocation_count++;
        return NULL;
    }

    total_size = payload_size + sizeof(VhMemHeader);
    old_size = vh_to_ulong(old_header->size);

    new_header = (VhMemHeader *)realloc(old_header, total_size);
    if (new_header == NULL) {
        g_stats.failed_allocation_count++;
        return NULL;
    }

    new_header->magic = VH_MEM_MAGIC;
    new_header->size = payload_size;
    new_tag_index = vh_find_or_add_tag(tag);
    new_header->tag_index = new_tag_index;
    new_size = vh_to_ulong(payload_size);

    if (g_stats.actual_current_heap_bytes >= old_size) {
        g_stats.actual_current_heap_bytes -= old_size;
    } else {
        g_stats.actual_current_heap_bytes = 0UL;
    }
    g_stats.actual_current_heap_bytes += new_size;
    vh_tag_sub_bytes(old_tag_index, old_size);
    vh_tag_add_bytes(new_tag_index, new_size);
    g_tag_stats[new_tag_index].realloc_count++;

    if (new_size > g_stats.largest_single_allocation_bytes) {
        g_stats.largest_single_allocation_bytes = new_size;
        g_largest_tag_index = new_tag_index;
        g_largest_operation = "realloc";
    }
    vh_update_peak();

    return (void *)(new_header + 1);
}

void *vh_realloc(void *ptr, size_t size)
{
    return vh_realloc_tag(ptr, size, "unknown");
}

void vh_free(void *ptr)
{
    VhMemHeader *header;
    unsigned long size;

    if (ptr == NULL) {
        return;
    }

    header = ((VhMemHeader *)ptr) - 1;
    if (header->magic != VH_MEM_MAGIC) {
        return;
    }

    size = vh_to_ulong(header->size);
    if (g_stats.actual_current_heap_bytes >= size) {
        g_stats.actual_current_heap_bytes -= size;
    } else {
        g_stats.actual_current_heap_bytes = 0UL;
    }
    g_stats.free_count++;
    vh_tag_sub_bytes(header->tag_index, size);

    header->magic = 0UL;
    header->size = 0U;
    free(header);
}

void vh_memtrack_print_summary(FILE *out,
                               const char *mode_name,
                               int candidate_count,
                               int selected_count,
                               int group_count,
                               int duplicate_group_count,
                               int largest_duplicate_group_size,
                               unsigned long current_before_cleanup,
                               const VhMemtrackStats *final_stats)
{
    const char *mode_text;

    if (out == NULL || final_stats == NULL) {
        return;
    }

    mode_text = (mode_name != NULL) ? mode_name : "unknown";

    fprintf(out, "Memory trace:\n");
    fprintf(out, "  mode: %s\n", mode_text);
    fprintf(out, "  candidate_count: %d\n", candidate_count);
    fprintf(out, "  selected_count: %d\n", selected_count);
    fprintf(out, "  group_count: %d\n", group_count);
    fprintf(out, "  duplicate_group_count: %d\n", duplicate_group_count);
    fprintf(out, "  largest_duplicate_group_size: %d\n", largest_duplicate_group_size);
    fprintf(out, "  actual_peak_heap_bytes: %lu\n", final_stats->actual_peak_heap_bytes);
    fprintf(out, "  actual_current_heap_bytes: %lu\n", current_before_cleanup);
    fprintf(out, "  actual_current_heap_bytes_after_cleanup: %lu\n", final_stats->actual_current_heap_bytes);
    fprintf(out, "  allocation_count: %lu\n", final_stats->allocation_count);
    fprintf(out, "  free_count: %lu\n", final_stats->free_count);
    fprintf(out, "  realloc_count: %lu\n", final_stats->realloc_count);
    fprintf(out, "  largest_single_allocation_bytes: %lu\n", final_stats->largest_single_allocation_bytes);
    fprintf(out, "  largest_single_allocation_tag: %s\n", vh_get_tag_name(g_largest_tag_index));
    fprintf(out, "  largest_single_allocation_operation: %s\n", g_largest_operation);
    fprintf(out, "  largest_single_allocation_is_payload_only: yes\n");
    fprintf(out, "  failed_allocation_count: %lu\n", final_stats->failed_allocation_count);
    fprintf(out, "  all_tracked_allocations_freed: %s\n", vh_memtrack_all_allocations_freed() ? "yes" : "no");

    if (g_tag_count > 0U) {
        unsigned short i;
        fprintf(out, "  allocations_by_tag:\n");
        for (i = 0; i < g_tag_count; ++i) {
            const VhTagStats *tag = &g_tag_stats[i];
            if ((tag->malloc_count + tag->realloc_count) == 0UL) {
                continue;
            }
            fprintf(out,
                    "    %s: malloc=%lu realloc=%lu peak_bytes=%lu current_bytes=%lu\n",
                    tag->name,
                    tag->malloc_count,
                    tag->realloc_count,
                    tag->peak_bytes,
                    tag->current_bytes);
        }
    }
}