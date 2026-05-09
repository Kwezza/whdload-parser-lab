/* src_raw/filtering/tlv_group.c - Group variants by canonical base name
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage G: real sorting and group-boundary detection.
 *
 * Two grouping paths are supported:
 *
 *   group_id path (has_group_id_field == 1):
 *     Sort by (group_id ASC, original_index ASC).  Boundary = change in
 *     group_id.  Used when the TLV field map contains group_id.
 *
 *   Fallback path (has_group_id_field == 0):
 *     Sort by (base_name ASC, original_index ASC).  Boundary = change in
 *     base_name.  Used for old TLVs that pre-date the group_id field.
 *
 * Within-group order is original TLV scan order (original_index), so
 * first-encountered always wins on score ties.
 *
 * C89-compatible; vbcc-safe.
 */

#include <filtering/tlv_group.h>
#include <filtering/tlv_filter.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* qsort comparator context                                               */
/*========================================================================*/

/* File-static pointer set by tlv_group_build() before calling qsort().
 * Not reentrant, but the harness is single-threaded.                     */
static const WhdVariantArray *s_sort_arr = NULL;

/*
 * group_id path comparator:
 *   primary   — group_id ascending
 *   secondary — original_index ascending (preserves TLV order within group)
 */
static int cmp_by_group_id(const void *a, const void *b)
{
    unsigned long  ia;
    unsigned long  ib;
    unsigned short ga;
    unsigned short gb;

    ia = *(const unsigned long *)a;
    ib = *(const unsigned long *)b;
    ga = s_sort_arr->items[ia].group_id;
    gb = s_sort_arr->items[ib].group_id;
    if (ga != gb) {
        return (ga < gb) ? -1 : 1;
    }
    if (s_sort_arr->items[ia].original_index !=
        s_sort_arr->items[ib].original_index) {
        return (s_sort_arr->items[ia].original_index <
                s_sort_arr->items[ib].original_index) ? -1 : 1;
    }
    return 0;
}

/*
 * Fallback comparator:
 *   primary   — base_name ascending (string)
 *   secondary — original_index ascending
 */
static int cmp_by_base_name(const void *a, const void *b)
{
    unsigned long  ia;
    unsigned long  ib;
    const char    *na;
    const char    *nb;
    int            cmp;

    ia = *(const unsigned long *)a;
    ib = *(const unsigned long *)b;
    na = s_sort_arr->items[ia].base_name;
    nb = s_sort_arr->items[ib].base_name;
    if (!na) { na = ""; }
    if (!nb) { nb = ""; }
    cmp = strcmp(na, nb);
    if (cmp != 0) {
        return cmp;
    }
    if (s_sort_arr->items[ia].original_index !=
        s_sort_arr->items[ib].original_index) {
        return (s_sort_arr->items[ia].original_index <
                s_sort_arr->items[ib].original_index) ? -1 : 1;
    }
    return 0;
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int tlv_group_build(WhdGroupSet           *gs,
                    const WhdVariantArray *arr,
                    int                    has_group_id_field)
{
    unsigned long  *si;     /* sorted_indices working buffer */
    WhdVariantGroup *grp;   /* groups working buffer         */
    unsigned long   i;
    unsigned long   n;
    unsigned long   pass;
    unsigned long   run_start;
    unsigned long   g;

    if (!gs) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    memset(gs, 0, sizeof(*gs));

    if (!arr || arr->count == 0u) {
        return WHD_FILTER_OK; /* zero variants — zero groups, not an error */
    }

    n = arr->count;

    /* 1. Allocate and fill sorted_indices */
    si = (unsigned long *)malloc(n * sizeof(unsigned long));
    if (!si) {
        return WHD_FILTER_ERR_OOM;
    }
    for (i = 0u; i < n; i++) {
        si[i] = i;
    }

    /* 2. Sort by the chosen key */
    s_sort_arr = arr;
    if (has_group_id_field) {
        qsort(si, n, sizeof(unsigned long), cmp_by_group_id);
    } else {
        qsort(si, n, sizeof(unsigned long), cmp_by_base_name);
    }
    s_sort_arr = NULL;

    /* 3. Count groups (first pass over sorted_indices) */
    g = 0u;
    run_start = 0u;
    for (i = 1u; i <= n; i++) {
        int boundary;

        boundary = (i == n);
        if (!boundary) {
            if (has_group_id_field) {
                boundary = (arr->items[si[i - 1u]].group_id !=
                            arr->items[si[i]].group_id);
            } else {
                const char *prev_name = arr->items[si[i - 1u]].base_name;
                const char *cur_name  = arr->items[si[i]].base_name;
                if (!prev_name) { prev_name = ""; }
                if (!cur_name)  { cur_name  = ""; }
                boundary = (strcmp(prev_name, cur_name) != 0);
            }
        }
        if (boundary) {
            g++;
            run_start = i;
        }
    }
    (void)run_start;

    /* 4. Allocate groups array */
    grp = (WhdVariantGroup *)malloc(g * sizeof(WhdVariantGroup));
    if (!grp) {
        free(si);
        return WHD_FILTER_ERR_OOM;
    }

    /* 5. Fill groups (second pass) */
    pass = 0u;
    run_start = 0u;
    for (i = 1u; i <= n; i++) {
        int boundary;

        boundary = (i == n);
        if (!boundary) {
            if (has_group_id_field) {
                boundary = (arr->items[si[i - 1u]].group_id !=
                            arr->items[si[i]].group_id);
            } else {
                const char *prev_name = arr->items[si[i - 1u]].base_name;
                const char *cur_name  = arr->items[si[i]].base_name;
                if (!prev_name) { prev_name = ""; }
                if (!cur_name)  { cur_name  = ""; }
                boundary = (strcmp(prev_name, cur_name) != 0);
            }
        }
        if (boundary) {
            grp[pass].group_name    = arr->items[si[run_start]].base_name;
            grp[pass].first_variant = run_start;
            grp[pass].variant_count = i - run_start;
            grp[pass].group_id      = has_group_id_field
                ? arr->items[si[run_start]].group_id
                : 0u;
            pass++;
            run_start = i;
        }
    }

    gs->sorted_indices      = si;
    gs->groups              = grp;
    gs->group_count         = g;
    gs->total_variants      = n;
    gs->used_group_id_field = has_group_id_field ? 1 : 0;
    gs->fallback_count      = has_group_id_field ? 0u : n;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

void tlv_group_free(WhdGroupSet *gs)
{
    if (!gs) {
        return;
    }
    free(gs->groups);
    free(gs->sorted_indices);
    gs->groups         = NULL;
    gs->sorted_indices = NULL;
    gs->group_count    = 0;
    gs->total_variants = 0;
}

/* End of Text */
