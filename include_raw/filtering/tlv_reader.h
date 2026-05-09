/* filtering/tlv_reader.h - Raw TLV binary loading and header validation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Owns loading the whole TLV file into a single buffer, validating the
 * outer binary structure, and exposing safe big-endian read helpers.
 * No higher-level parsing (field map, variant scan) happens here.
 *
 * TLV disk values are big-endian (Motorola order).  Host builds must
 * byte-swap where needed; Amiga builds can read directly.
 */

#ifndef FILTERING_TLV_READER_H
#define FILTERING_TLV_READER_H

#include <platform.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Reader state                                                           */

typedef struct TlvReader {
    uint8_t       *buffer;  /* entire file loaded into memory */
    unsigned long  size;    /* byte count of buffer           */
    unsigned long  pos;     /* current read cursor            */
} TlvReader;

/*------------------------------------------------------------------------*/
/* Header info filled by tlv_reader_validate_header                      */

typedef struct TlvHeaderInfo {
    uint16_t version;       /* TLV file format version */
    uint32_t record_count;  /* number of data records in file */
} TlvHeaderInfo;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Load entire TLV file into a single buffer.
 * Returns WHD_FILTER_OK or a negative error code. */
int  tlv_reader_load(TlvReader *reader, const char *path);

/* Release buffer memory.  Safe to call on a zeroed struct. */
void tlv_reader_free(TlvReader *reader);

/* Validate magic / version / endian marker in the loaded buffer.
 * Fills *info if non-NULL.
 * Returns WHD_FILTER_OK or a negative error code. */
int  tlv_reader_validate_header(const TlvReader *reader, TlvHeaderInfo *info);

/*------------------------------------------------------------------------*/
/* Big-endian read helpers                                                */

uint16_t tlv_read_u16_be(const uint8_t *p);
uint32_t tlv_read_u32_be(const uint8_t *p);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_READER_H */
/* End of Text */
