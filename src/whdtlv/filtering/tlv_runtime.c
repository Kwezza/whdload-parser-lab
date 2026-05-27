/* src_raw/filtering/tlv_runtime.c - In-memory TLV state
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Parses the top-level blocks from the loaded TLV buffer:
 *
 *   Block 0x01  Metadata map  (field_id -> name pairs, BE 2-byte size)
 *   Block 0x04  CSV CRC fingerprints (BE 2-byte size + 2-byte count + entries)
 *   0x04..0xFF  Dynamic data records  (data_offset marks their start)
 *
 * All multi-byte values are big-endian (Motorola byte order, native to the 68000).
 */

#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_filter.h"
#include <stdlib.h>
#include <string.h>

/* TLV block type bytes */
#define TLV_TYPE_METADATA_MAP     0x01u
#define TLV_TYPE_GROUP_MAP        0x02u
#define TLV_TYPE_CSV_FINGERPRINTS 0x04u
/* Field IDs for data records occupy 0x04..0xFF, same as fingerprint type.
 * The fingerprint block appears immediately after the field map and before
 * any data records, so parsing order disambiguates. */

/*------------------------------------------------------------------------*/
/* Field map parser (type 0x01 block)                                    */
/*
 * Wire layout:
 *   [1]  type byte  (already consumed before calling this)
 *   [2]  map_size   BE  -- payload byte count
 *   ...  entries until map_size bytes consumed:
 *          [1]  field_id
 *          [N+1] NUL-terminated field_name
 */

static int parse_field_map(TlvRuntime *rt, unsigned long *pos)
{
    uint16_t      map_size;
    unsigned long end;
    uint8_t       count;
    uint8_t       field_id;
    unsigned long name_start;
    unsigned long name_len;
    const uint8_t *buf = rt->reader.buffer;
    unsigned long  sz  = rt->reader.size;

    if (*pos + 2u > sz) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    map_size = tlv_read_u16_be(buf + *pos);
    *pos += 2u;

    end   = *pos + map_size;
    count = 0;

    while (*pos < end && *pos < sz) {
        if (*pos + 1u > sz) {
            break;
        }
        field_id = buf[*pos];
        *pos += 1u;

        /* Scan NUL-terminated name */
        name_start = *pos;
        while (*pos < end && *pos < sz && buf[*pos] != '\0') {
            *pos += 1u;
        }
        if (*pos >= sz) {
            break;
        }
        name_len = *pos - name_start;
        *pos += 1u; /* consume NUL */

        if (count < TLV_RUNTIME_MAX_FIELDS) {
            rt->field_map.entries[count].id = field_id;
            if (name_len >= TLV_RUNTIME_FIELD_NAME_MAX) {
                name_len = TLV_RUNTIME_FIELD_NAME_MAX - 1u;
            }
            memcpy(rt->field_map.entries[count].name,
                   buf + name_start,
                   name_len);
            rt->field_map.entries[count].name[name_len] = '\0';
            count++;
        }
    }

    /* Advance past any remaining payload bytes we may have not consumed */
    if (*pos < end) {
        *pos = end;
    }

    rt->field_map.count = count;
    rt->has_field_map   = 1;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* CSV CRC fingerprint parser (type 0x04 block)                          */
/*
 * Wire layout:
 *   [1]  type byte  (already consumed before calling this)
 *   [2]  payload_size  BE
 *   [2]  count         BE
 *   per entry:
 *     [N+1]  NUL-terminated csv_name
 *     [4]    crc32  BE
 */

static int parse_crc_block(TlvRuntime *rt, unsigned long *pos)
{
    uint16_t      payload_size;
    uint16_t      count;
    uint16_t      i;
    unsigned long end;
    unsigned long name_start;
    unsigned long name_len;
    const uint8_t *buf = rt->reader.buffer;
    unsigned long  sz  = rt->reader.size;

    if (*pos + 2u > sz) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    payload_size = tlv_read_u16_be(buf + *pos);
    *pos += 2u;

    end = *pos + payload_size;

    if (*pos + 2u > sz) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    count = tlv_read_u16_be(buf + *pos);
    *pos += 2u;

    if (count == 0u) {
        *pos = end;
        rt->has_crc_map = 1;
        return WHD_FILTER_OK;
    }

    rt->crc_map.entries = (TlvCrcEntry *)malloc(count * sizeof(TlvCrcEntry));
    if (!rt->crc_map.entries) {
        return WHD_FILTER_ERR_OOM;
    }
    memset(rt->crc_map.entries, 0, count * sizeof(TlvCrcEntry));

    for (i = 0; i < count && *pos < end && *pos < sz; i++) {
        /* NUL-terminated csv_name */
        name_start = *pos;
        while (*pos < end && *pos < sz && buf[*pos] != '\0') {
            *pos += 1u;
        }
        if (*pos >= sz) {
            break;
        }
        name_len = *pos - name_start;
        *pos += 1u; /* consume NUL */

        if (name_len >= TLV_RUNTIME_CSV_NAME_MAX) {
            name_len = TLV_RUNTIME_CSV_NAME_MAX - 1u;
        }
        memcpy(rt->crc_map.entries[i].csv_name, buf + name_start, name_len);
        rt->crc_map.entries[i].csv_name[name_len] = '\0';

        /* 4-byte BE CRC */
        if (*pos + 4u > sz) {
            break;
        }
        rt->crc_map.entries[i].crc32 = tlv_read_u32_be(buf + *pos);
        *pos += 4u;
    }

    rt->crc_map.count = i;
    *pos = end;  /* advance past any trailing bytes in the block */

    rt->has_crc_map = 1;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Group map parser (type 0x02 block)                                    */
/*
 * Wire layout (written by tlv_write_group_map):
 *   [1]  type byte  (already consumed before calling this)
 *   [2]  payload_size  BE
 *   [2]  group_count   BE
 *   per entry:
 *     [2]  group_id   BE
 *     [1]  name_len
 *     [name_len bytes]  group_name (no NUL terminator in file)
 */

static int parse_group_map(TlvRuntime *rt, unsigned long *pos)
{
    uint16_t       payload_size;
    uint16_t       count;
    uint16_t       i;
    unsigned long  end;
    const uint8_t *buf = rt->reader.buffer;
    unsigned long  sz  = rt->reader.size;

    if (*pos + 2u > sz) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    payload_size = tlv_read_u16_be(buf + *pos);
    *pos += 2u;

    end = *pos + payload_size;

    if (*pos + 2u > sz) {
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    count = tlv_read_u16_be(buf + *pos);
    *pos += 2u;

    if (count == 0u) {
        *pos = end;
        rt->has_group_map = 1;
        return WHD_FILTER_OK;
    }

    rt->group_map.entries = (TlvGroupEntry *)malloc(
        (unsigned long)count * sizeof(TlvGroupEntry));
    if (!rt->group_map.entries) {
        return WHD_FILTER_ERR_OOM;
    }
    memset(rt->group_map.entries, 0,
           (unsigned long)count * sizeof(TlvGroupEntry));

    for (i = 0u; i < count && *pos < end && *pos < sz; i++) {
        uint16_t  gid;
        uint8_t   name_len_byte;
        unsigned long name_len;

        /* 2-byte BE group_id */
        if (*pos + 2u > sz) {
            break;
        }
        gid    = tlv_read_u16_be(buf + *pos);
        *pos  += 2u;

        /* 1-byte name length */
        if (*pos >= sz) {
            break;
        }
        name_len_byte = buf[*pos];
        *pos += 1u;

        name_len = (unsigned long)name_len_byte;
        if (name_len >= TLV_RUNTIME_GROUP_NAME_MAX) {
            name_len = TLV_RUNTIME_GROUP_NAME_MAX - 1u;
        }

        if (*pos + name_len > sz) {
            break;
        }

        rt->group_map.entries[i].id = gid;
        if (name_len > 0u) {
            memcpy(rt->group_map.entries[i].name, buf + *pos, name_len);
        }
        rt->group_map.entries[i].name[name_len] = '\0';
        *pos += name_len_byte; /* advance by the on-disk length, not capped value */
    }

    rt->group_map.count = i;
    *pos = end; /* advance past any trailing bytes */

    rt->has_group_map = 1;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* API                                                                    */

void tlv_runtime_init(TlvRuntime *rt)
{
    if (!rt) {
        return;
    }
    memset(rt, 0, sizeof(*rt));
}

/*------------------------------------------------------------------------*/

int tlv_runtime_load(TlvRuntime *rt, const char *path)
{
    unsigned long pos;
    uint8_t       type_byte;
    int           rc;

    if (!rt || !path) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    rc = tlv_reader_load(&rt->reader, path);
    if (rc != WHD_FILTER_OK) {
        return rc;
    }

    rc = tlv_reader_validate_header(&rt->reader, &rt->header);
    if (rc != WHD_FILTER_OK) {
        tlv_reader_free(&rt->reader);
        return rc;
    }

    /* Walk the preamble blocks (before data records).
     *
     * Expected order produced by the writer:
     *   pos 0:  type 0x01  (metadata map)
     *   then:   type 0x04  (CSV fingerprints)
     *   then:   data records
     *
     * We parse blocks as long as we see reserved types (0x01..0x03).
     * Type 0x04 is both a reserved type AND the lowest dynamic field id;
     * disambiguation is by position: the first 0x04 encountered in the
     * preamble is the fingerprint block, subsequent ones are data fields.
     */

    pos = 0u;
    rc  = WHD_FILTER_OK;

    /* --- Block 0x01: metadata map --- */
    type_byte = rt->reader.buffer[pos];
    pos += 1u;
    if (type_byte != TLV_TYPE_METADATA_MAP) {
        tlv_reader_free(&rt->reader);
        return WHD_FILTER_ERR_TLV_HEADER;
    }
    rc = parse_field_map(rt, &pos);
    if (rc != WHD_FILTER_OK) {
        tlv_reader_free(&rt->reader);
        return rc;
    }

    /* --- Optional block 0x02: group map ---
     * Must be checked before 0x04 because new TLVs write blocks in order
     * 0x01, 0x02, 0x04.  Old TLVs without block 0x02 skip this branch and
     * fall through to the CRC block check. */
    if (pos < rt->reader.size) {
        type_byte = rt->reader.buffer[pos];
        if (type_byte == TLV_TYPE_GROUP_MAP) {
            pos += 1u;
            rc = parse_group_map(rt, &pos);
            if (rc != WHD_FILTER_OK) {
                tlv_reader_free(&rt->reader);
                return rc;
            }
        }
    }

    /* --- Optional block 0x04: CSV CRC fingerprints --- */
    if (pos < rt->reader.size) {
        type_byte = rt->reader.buffer[pos];
        if (type_byte == TLV_TYPE_CSV_FINGERPRINTS) {
            pos += 1u;
            rc = parse_crc_block(rt, &pos);
            if (rc != WHD_FILTER_OK) {
                tlv_reader_free(&rt->reader);
                return rc;
            }
        }
    }

    /* Resolve group_id field ID from the embedded field map */
    rt->group_id_field_id = tlv_runtime_field_id(rt, "group_id");

    rt->data_offset = pos;
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/

void tlv_runtime_free(TlvRuntime *rt)
{
    if (!rt) {
        return;
    }
    tlv_reader_free(&rt->reader);
    if (rt->crc_map.entries) {
        free(rt->crc_map.entries);
        rt->crc_map.entries = NULL;
    }
    if (rt->group_map.entries) {
        free(rt->group_map.entries);
        rt->group_map.entries = NULL;
    }
    memset(rt, 0, sizeof(*rt));
}

/*------------------------------------------------------------------------*/

uint8_t tlv_runtime_field_id(const TlvRuntime *rt, const char *field_name)
{
    uint8_t i;
    if (!rt || !field_name) {
        return 0u;
    }
    for (i = 0u; i < rt->field_map.count; i++) {
        if (strcmp(rt->field_map.entries[i].name, field_name) == 0) {
            return rt->field_map.entries[i].id;
        }
    }
    return 0u;
}

/*------------------------------------------------------------------------*/

const char *tlv_runtime_field_name(const TlvRuntime *rt, uint8_t field_id)
{
    uint8_t i;
    if (!rt) {
        return NULL;
    }
    for (i = 0u; i < rt->field_map.count; i++) {
        if (rt->field_map.entries[i].id == field_id) {
            return rt->field_map.entries[i].name;
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------*/

const char *tlv_runtime_group_name(const TlvRuntime *rt, uint16_t group_id)
{
    unsigned long i;
    if (!rt || !rt->has_group_map || !rt->group_map.entries) {
        return NULL;
    }
    for (i = 0u; i < rt->group_map.count; i++) {
        if (rt->group_map.entries[i].id == group_id) {
            return rt->group_map.entries[i].name;
        }
    }
    return NULL;
}

/* End of Text */
