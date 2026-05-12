/* filtering/tlv_group.h - Group variants by canonical base name
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Produces a compact array of WhdVariantGroup descriptors that index into
 * a companion sorted_indices array over a WhdVariantArray.
 *
 * Avoids linked lists.  Groups are built by sorting a companion index
 * array by base_name and then recording contiguous-run boundaries.
 *
 * Within-group order is the original TLV order (first-encountered wins
 * on tie-break during selection).
 */

#ifndef FILTERING_TLV_GROUP_H
#define FILTERING_TLV_GROUP_H

#include "platform.h"
#include "whdtlv/filtering/tlv_variant.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Group descriptor                                                       */

typedef struct WhdVariantGroup {
    const char    *group_name;    /* points into first variant's base_name */
    unsigned long  first_variant; /* index into sorted_indices array        */
    unsigned long  variant_count;
    unsigned short group_id;      /* 0 on fallback path; group_id on ID path */
} WhdVariantGroup;

/*------------------------------------------------------------------------*/
/* Group set                                                              */

typedef struct WhdGroupSet {
    WhdVariantGroup *groups;          /* array of group descriptors        */
    unsigned long   *sorted_indices;  /* variant indices sorted by grouping key */
    unsigned long    group_count;
    unsigned long    total_variants;  /* == source WhdVariantArray.count   */
    int              used_group_id_field; /* 1=grouped by group_id; 0=fallback */
    unsigned long    fallback_count;  /* variants using fallback derivation */
} WhdGroupSet;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Build a WhdGroupSet from a variant array.
 * has_group_id_field: pass 1 if the TLV field map contains group_id and
 *   variants carry numeric group IDs; pass 0 to use the display_name
 *   heuristic fallback.
 * Returns WHD_FILTER_OK or a negative error code. */
int tlv_group_build(WhdGroupSet           *gs,
                    const WhdVariantArray *arr,
                    int                    has_group_id_field);

/* Release all memory owned by gs.  Does not free gs itself. */
void tlv_group_free(WhdGroupSet *gs);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_GROUP_H */
/* End of Text */
