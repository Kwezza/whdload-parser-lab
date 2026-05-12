/* filtering/tlv_variant.h - Lightweight variant views over a TLV buffer
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Each TLV archive record is exposed as a WhdVariantView.  Fields point
 * into the loaded TLV buffer where safe to avoid unnecessary copying.
 * Strings that need normalisation (base_name) are owned by the view.
 *
 * Rules:
 *   - filename       is an owned, heap-allocated NUL-terminated copy
 *   - base_name      is an owned, heap-allocated NUL-terminated copy
 *                    (canonical group name derived by derive_group_name;
 *                     used for fallback grouping on old TLVs)
 *   - group_id       uint16 read from the TLV group_id field (0 = absent)
 *   - original_index 0-based scan order; runtime-only, never stored in TLV
 *   - field values   point into the TLV buffer (not owned)
 */

#ifndef FILTERING_TLV_VARIANT_H
#define FILTERING_TLV_VARIANT_H

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Field / value pair within a variant view                              */

typedef struct WhdTlvFieldValue {
    uint8_t        field_id;
    uint16_t       length;
    const uint8_t *value;   /* points into TLV buffer - not owned */
} WhdTlvFieldValue;

/*------------------------------------------------------------------------*/
/* Single variant view                                                    */

#define WHD_VARIANT_MAX_FIELDS 32

typedef struct WhdVariantView {
    char             *filename;        /* owned heap copy, NUL-terminated    */
    char             *base_name;      /* owned heap copy                    */
    unsigned short    group_id;       /* from TLV group_id field; 0=absent  */
    unsigned char     has_group_id;   /* 1 if group_id was present in TLV   */
    /* 1 byte implicit padding on 4-byte aligned platforms */
    unsigned long     original_index; /* 0-based scan order (runtime-only)  */
    unsigned short    variant_index;
    unsigned short    field_count;
    unsigned short    interior_fields;
    WhdTlvFieldValue  fields[WHD_VARIANT_MAX_FIELDS];
} WhdVariantView;

/*------------------------------------------------------------------------*/
/* View array                                                             */

typedef struct WhdVariantArray {
    WhdVariantView *items;
    unsigned long   count;
    unsigned long   capacity;
} WhdVariantArray;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * Build a WhdVariantArray from a raw TLV buffer.
 * display_field_id  is the field ID used as the variant boundary marker.
 * group_id_field_id is the field ID for the numeric group key (0 = absent;
 *                   pass 0 for old TLVs without group_id).
 * Returns WHD_FILTER_OK or a negative error code.
 */
int tlv_variant_build(WhdVariantArray *arr,
                      const uint8_t   *buffer,
                      unsigned long    buf_size,
                      uint8_t          display_field_id,
                      uint8_t          group_id_field_id);

/*
 * Find a field by numeric ID within a single variant view.
 * Returns a pointer to the matching field, or NULL.
 */
const WhdTlvFieldValue *tlv_variant_find_field(const WhdVariantView *v,
                                               uint8_t               field_id);

/* Release all memory owned by arr.  Does not free arr itself. */
void tlv_variant_free(WhdVariantArray *arr);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_VARIANT_H */
/* End of Text */
