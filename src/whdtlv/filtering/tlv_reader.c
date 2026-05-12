/* src_raw/filtering/tlv_reader.c - Raw TLV binary loading
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * The existing TLV writer uses raw fwrite() on host (x86/x64), so all
 * multi-byte fields on disk are little-endian.  The read helpers below
 * follow the same byte order to match actual on-disk data.
 *
 * The plan's "big-endian" comment describes the aspirational Amiga-native
 * format; the current files produced by tlv_builder.c are little-endian.
 *
 * TLV type-byte constants (from tlv_builder.h):
 *   0x01  metadata map (field_id -> name table)
 *   0x02  record count  (not yet written by builder)
 *   0x03  file version  (not yet written by builder)
 *   0x04  CSV fingerprints
 *   0x04..0xFF  dynamic field data records
 *
 * The first block in a well-formed TLV must be type 0x01.
 */

#include "whdtlv/filtering/tlv_reader.h"
#include "whdtlv/filtering/tlv_filter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* type byte that marks the start of the embedded field map */
#define TLV_TYPE_METADATA_MAP   0x01u

/*------------------------------------------------------------------------*/
/* Big-endian read helpers (declared in header for future Amiga use)     */

/*------------------------------------------------------------------------*/
/* Load                                                                   */

int tlv_reader_load(TlvReader *reader, const char *path)
{
    FILE         *f;
    long          sz;
    size_t        n;

    if (!reader || !path) {
        return WHD_FILTER_ERR_BAD_ARG;
    }
    memset(reader, 0, sizeof(*reader));

    f = fopen(path, "rb");
    if (!f) {
        return WHD_FILTER_ERR_TLV_OPEN;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return WHD_FILTER_ERR_TLV_OPEN;
    }
    sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    rewind(f);

    reader->buffer = (uint8_t *)malloc((size_t)sz);
    if (!reader->buffer) {
        fclose(f);
        return WHD_FILTER_ERR_OOM;
    }

    n = fread(reader->buffer, 1, (size_t)sz, f);
    fclose(f);

    if (n != (size_t)sz) {
        free(reader->buffer);
        reader->buffer = NULL;
        return WHD_FILTER_ERR_TLV_OPEN;
    }

    reader->size = (unsigned long)sz;
    reader->pos  = 0;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Free                                                                   */

void tlv_reader_free(TlvReader *reader)
{
    if (!reader) {
        return;
    }
    if (reader->buffer) {
        free(reader->buffer);
        reader->buffer = NULL;
    }
    reader->size = 0;
    reader->pos  = 0;
}

/*------------------------------------------------------------------------*/
/* Validate header                                                        */
/*
 * A well-formed TLV file begins with type byte 0x01 (metadata map).
 * There is currently no magic number or explicit version record in the
 * on-disk format; we synthesise version 1 from the constant TLV_FILE_VERSION.
 *
 * record_count in TlvHeaderInfo is left at 0 here - the actual variant
 * count is determined during the variant scan in Stage F.
 */

int tlv_reader_validate_header(const TlvReader *reader, TlvHeaderInfo *info)
{
    if (!reader || !reader->buffer || reader->size < 3u) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    if (reader->buffer[0] != TLV_TYPE_METADATA_MAP) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    if (info) {
        memset(info, 0, sizeof(*info));
        info->version      = 1u;  /* TLV_FILE_VERSION from tlv_builder.h */
        info->record_count = 0u;  /* filled later during variant scan     */
    }
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Big-endian read helpers (declared in header for future Amiga use)     */

uint16_t tlv_read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t tlv_read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
            (uint32_t)p[3];
}

/*------------------------------------------------------------------------*/
/* Internal LE helpers exposed for tlv_runtime.c via static linkage      */
/* (not in the public header; tlv_runtime.c includes this .c directly is */
/* not an option - instead we duplicate the two small helpers there).    */

/* End of Text */
