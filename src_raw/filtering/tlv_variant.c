/* src_raw/filtering/tlv_variant.c - Lightweight variant views over a TLV buffer
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage F: real TLV record scanner.
 *
 * TLV data record wire format (little-endian):
 *   per entry:
 *     [1]  field_id
 *     [2]  LE uint16 value length
 *     [N]  value bytes
 *
 * Variants are delimited by the display_field_id boundary marker.  Every
 * display_field_id entry starts a new variant.  Its value is the sanitised
 * filename (no extension) stored as raw bytes, length-delimited (no NUL in
 * the buffer).  We store a pointer into the buffer and provide a safe
 * NUL-terminated copy for base_name.
 *
 * Field values for CSV-backed fields are 4-byte LE uint32 IDs.  The view
 * stores the raw bytes and lets the scorer decode them.
 *
 * base_name derivation:
 *   Canonical group name derived by whdtlv_derive_group_name() (group_util.h).
 *   Used for fallback grouping when group_id is not present in the TLV.
 *   When group_id IS present, base_name is still populated but grouping
 *   uses the numeric group_id instead.
 *
 * group_id:
 *   When group_id_field_id != 0, interior fields are scanned for the
 *   group_id field ID.  Its 2-byte big-endian payload is stored in
 *   WhdVariantView.group_id.  0 means absent.
 *
 * original_index:
 *   0-based scan order assigned as each variant is discovered.  Runtime-
 *   only; never stored in the TLV.  Used as a deterministic secondary sort
 *   key so tie-breaks preserve first-encountered TLV order.
 *
 * C89-compatible; vbcc-safe.
 * - Variables declared at block top.
 * - No VLAs, no for-loop init declarations.
 */

#include <filtering/tlv_variant.h>
#include <filtering/tlv_filter.h>
#include <group_util.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Internal helpers                                                       */
/*========================================================================*/

/*------------------------------------------------------------------------*/
/* LE helpers                                                             */

static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/*------------------------------------------------------------------------*/
/* Grow the variant array by doubling capacity.
 * Returns 1 on success, 0 on OOM.                                       */

static int arr_grow(WhdVariantArray *arr)
{
    unsigned long   new_cap;
    WhdVariantView *new_items;

    new_cap = arr->capacity ? arr->capacity * 2u : 1024u;
    new_items = (WhdVariantView *)malloc(new_cap * sizeof(WhdVariantView));
    if (!new_items) {
        return 0;
    }
    if (arr->items && arr->count) {
        memcpy(new_items, arr->items, arr->count * sizeof(WhdVariantView));
    }
    free(arr->items);
    arr->items    = new_items;
    arr->capacity = new_cap;
    return 1;
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int tlv_variant_build(WhdVariantArray *arr,
                      const uint8_t   *buffer,
                      unsigned long    buf_size,
                      uint8_t          display_field_id,
                      uint8_t          group_id_field_id)
{
    unsigned long   pos;
    unsigned long   variant_start; /* byte offset where current variant started */
    WhdVariantView *cur;           /* pointer to current variant being filled   */
    int             in_variant;    /* 1 when we are inside a variant block      */

    if (!arr) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    memset(arr, 0, sizeof(*arr));

    if (!buffer || buf_size == 0u) {
        return WHD_FILTER_OK; /* empty buffer — zero variants, not an error */
    }

    pos        = 0u;
    in_variant = 0;
    cur        = NULL;
    variant_start = 0u;

    while (pos + 3u <= buf_size) {
        uint8_t        field_id;
        uint16_t       length;
        unsigned long  value_start;

        field_id    = buffer[pos];
        length      = u16_le(buffer + pos + 1u);
        value_start = pos + 3u;

        /* Safety: skip entry if it would run past the end of the buffer */
        if (value_start + (unsigned long)length > buf_size) {
            break;
        }

        /* -- New variant boundary ---------------------------------------- */
        if (field_id == display_field_id) {
            const char    *name_ptr;
            unsigned long  name_len;

            /* Allocate slot */
            if (arr->count >= arr->capacity) {
                if (!arr_grow(arr)) {
                    tlv_variant_free(arr);
                    return WHD_FILTER_ERR_OOM;
                }
            }

            cur = &arr->items[arr->count++];
            memset(cur, 0, sizeof(*cur));
            cur->variant_index  = (unsigned short)(arr->count - 1u);
            cur->original_index = arr->count - 1u;

            /* filename points into the buffer (not NUL-terminated, but we
             * record it as a pointer; callers use the length from the entry) */
            name_ptr = (const char *)(buffer + value_start);
            name_len = (unsigned long)length;

            /* Store a NUL-terminated pointer into the buffer safely.
             * The TLV buffer is heap-allocated and lives for the duration of
             * the run, so pointing into it is safe.  However the value is NOT
             * NUL-terminated in the buffer.  We need to allocate a copy for
             * the filename string so callers can use it as a C string.
             * Also allocate base_name (separate, possibly shorter).         */

            /* filename — owned NUL-terminated copy */
            {
                char *fn_copy = (char *)malloc(name_len + 1u);
                if (!fn_copy) {
                    tlv_variant_free(arr);
                    return WHD_FILTER_ERR_OOM;
                }
                memcpy(fn_copy, name_ptr, name_len);
                fn_copy[name_len] = '\0';
                cur->filename = fn_copy; /* we own this */

                /* base_name — canonical group name via whdtlv_derive_group_name */
                {
                    char          derived[128]; /* matches GROUP_MAP_NAME_MAX */
                    unsigned long dlen;
                    char         *base_copy;

                    dlen      = whdtlv_derive_group_name(fn_copy, derived, sizeof(derived));
                    base_copy = (char *)malloc(dlen + 1u);
                    if (!base_copy) {
                        tlv_variant_free(arr);
                        return WHD_FILTER_ERR_OOM;
                    }
                    memcpy(base_copy, derived, dlen + 1u); /* includes NUL */
                    cur->base_name = base_copy;
                }
            }

            in_variant    = 1;
            variant_start = pos;
            (void)variant_start; /* used logically; silence warning */
        }
        /* -- Interior field --------------------------------------------- */
        else if (in_variant && cur) {
            /* Special: read group_id from its 2-byte big-endian payload.
             * Do NOT store in fields[] — group_id is structural metadata
             * and must not influence interior_fields or profile scoring. */
            if (group_id_field_id != 0u &&
                field_id          == group_id_field_id &&
                length            == 2u) {
                cur->group_id     =
                    (unsigned short)(((unsigned short)buffer[value_start]      << 8) |
                                     (unsigned short)buffer[value_start + 1u]);
                cur->has_group_id = 1;
            }
            else if (cur->field_count < WHD_VARIANT_MAX_FIELDS) {
                WhdTlvFieldValue *fv = &cur->fields[cur->field_count++];
                fv->field_id = field_id;
                fv->length   = length;
                fv->value    = buffer + value_start;
            }
            cur->interior_fields = cur->field_count;
        }

        pos = value_start + (unsigned long)length;
    }

    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

const WhdTlvFieldValue *tlv_variant_find_field(const WhdVariantView *v,
                                               uint8_t               field_id)
{
    unsigned short i;
    if (!v) {
        return NULL;
    }
    for (i = 0; i < v->field_count; i++) {
        if (v->fields[i].field_id == field_id) {
            return &v->fields[i];
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------*/

void tlv_variant_free(WhdVariantArray *arr)
{
    unsigned long i;
    if (!arr) {
        return;
    }
    if (arr->items) {
        for (i = 0; i < arr->count; i++) {
            free((char *)arr->items[i].filename);
            arr->items[i].filename = NULL;
            free(arr->items[i].base_name);
            arr->items[i].base_name = NULL;
        }
        free(arr->items);
        arr->items = NULL;
    }
    arr->count    = 0;
    arr->capacity = 0;
}

/* End of Text */
