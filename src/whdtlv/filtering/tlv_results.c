/* src_raw/filtering/tlv_results.c - Write filter results to disk and print summary
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage I: real file writing.
 *
 * Output format: one selected archive filename per line, no header.
 * Rejected groups (all variants excluded) are silently skipped.
 * Groups with no selection (WHD_NO_SELECTION) are silently skipped.
 *
 * C89-compatible; vbcc-safe.
 */

#include "whdtlv/filtering/tlv_results.h"
#include "whdtlv/filtering/tlv_select.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int tlv_results_write_file(const char            *output_path,
                           const WhdSelectResult *sel,
                           const WhdGroupSet     *gs,
                           const WhdVariantArray *arr)
{
    FILE         *f;
    unsigned long g;

    (void)gs; /* gs not needed when variant_index is a direct arr index */

    if (!output_path || !sel || !arr) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    f = fopen(output_path, "w");
    if (!f) {
        return WHD_FILTER_ERR_OUTPUT_WRITE;
    }

    for (g = 0u; g < sel->count; g++) {
        const WhdSelectEntry *entry = &sel->entries[g];
        uint8_t               li;

        if (entry->all_rejected || entry->lane_selected_count == 0u) {
            continue;
        }

        for (li = 0u; li < entry->lane_selected_count; li++) {
            unsigned long        arr_idx  = entry->selected_indices[li];
            const char          *filename;

            if (arr_idx == WHD_NO_SELECTION) {
                continue;
            }

            filename = arr->items[arr_idx].filename;
            if (!filename) {
                continue;
            }

            fprintf(f, "%s\n", filename);
        }
    }

    fclose(f);
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

int tlv_results_collect_list(const WhdSelectResult *sel,
                              const WhdGroupSet     *gs,
                              const WhdVariantArray *arr,
                              WhdTlvStringList      *list)
{
    unsigned long  g;
    unsigned long  count;
    size_t         string_bytes;
    size_t         alloc_size;
    char          *block;
    char         **ptr_table;
    char          *str_area;
    size_t         str_pos;
    unsigned long  pi;

    (void)gs;

    if (!sel || !arr || !list) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    /* Safe empty state before any work. */
    list->count    = 0u;
    list->items    = NULL;
    list->reserved = NULL;

    /* --- Pass 1: count filenames and total string bytes. --- */
    count        = 0u;
    string_bytes = 0u;

    for (g = 0u; g < sel->count; g++) {
        const WhdSelectEntry *entry = &sel->entries[g];
        uint8_t               li;

        if (entry->all_rejected || entry->lane_selected_count == 0u) {
            continue;
        }

        for (li = 0u; li < entry->lane_selected_count; li++) {
            unsigned long arr_idx = entry->selected_indices[li];
            const char   *fn;

            if (arr_idx == WHD_NO_SELECTION) {
                continue;
            }
            fn = arr->items[arr_idx].filename;
            if (!fn) {
                continue;
            }
            count++;
            string_bytes += strlen(fn) + 1u; /* +1 for NUL */
        }
    }

    /* Empty result set is valid. */
    if (count == 0u) {
        return WHD_FILTER_OK;
    }

    /* --- Single allocation: pointer table followed by packed string data. --- */
    alloc_size = count * sizeof(char *) + string_bytes;
    block = (char *)malloc(alloc_size);
    if (!block) {
        return WHD_FILTER_ERR_OOM;
    }

    ptr_table = (char **)block;
    str_area  = block + count * sizeof(char *);
    str_pos   = 0u;
    pi        = 0u;

    /* --- Pass 2: fill pointer table and copy string data. --- */
    for (g = 0u; g < sel->count; g++) {
        const WhdSelectEntry *entry = &sel->entries[g];
        uint8_t               li;

        if (entry->all_rejected || entry->lane_selected_count == 0u) {
            continue;
        }

        for (li = 0u; li < entry->lane_selected_count; li++) {
            unsigned long arr_idx = entry->selected_indices[li];
            const char   *fn;
            size_t        fn_len;

            if (arr_idx == WHD_NO_SELECTION) {
                continue;
            }
            fn = arr->items[arr_idx].filename;
            if (!fn) {
                continue;
            }

            fn_len = strlen(fn);
            memcpy(str_area + str_pos, fn, fn_len + 1u);
            ptr_table[pi] = str_area + str_pos;
            str_pos += fn_len + 1u;
            pi++;
        }
    }

    list->count    = (unsigned int)count;
    list->items    = ptr_table;
    list->reserved = block;

    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

void tlv_results_print_summary(const char            *tlv_path,
                               const char            *profile_path,
                               const char            *output_path,
                               const WhdFilterResult *result)
{
    if (!result) {
        return;
    }
    printf("TLV     : %s\n",  tlv_path     ? tlv_path     : "(none)");
    printf("Profile : %s\n",  profile_path ? profile_path : "(none)");
    printf("Variants: %lu\n", result->total_variants);
    printf("Groups  : %lu\n", result->total_groups);
    printf("Selected variants : %lu\n", result->total_selected_variants);
    printf("Selected groups   : %lu\n", result->selected_count);
    if (result->lane_count > 1u) {
        printf("Selection lanes   : %lu\n", result->lane_count);
    }
    printf("Variants rejected: %lu\n",  result->rejected_variants_count);
    printf("Groups rejected  : %lu\n",  result->rejected_groups_count);
    if (result->had_warnings) {
        printf("Warnings: yes\n");
    }
    if (output_path) {
        printf("Output  : %s\n", output_path);
    }
}

/* End of Text */
