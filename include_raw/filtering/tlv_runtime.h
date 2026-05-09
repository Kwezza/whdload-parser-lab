/* filtering/tlv_runtime.h - In-memory TLV state owner
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * TlvRuntime owns the loaded TLV buffer and all views derived from it.
 * It is built once per run and kept alive until filtering completes.
 *
 * The field map and CRC map are defined here as lightweight self-contained
 * types so this header does not depend on the old pipeline headers.
 *
 * TLV on-disk byte order: little-endian (matches the existing writer which
 * uses raw fwrite without byte-swapping on the host).
 */

#ifndef FILTERING_TLV_RUNTIME_H
#define FILTERING_TLV_RUNTIME_H

#include <platform.h>
#include <filtering/tlv_reader.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Field map (parsed from type-0x01 block)                               */

#define TLV_RUNTIME_MAX_FIELDS 252
#define TLV_RUNTIME_FIELD_NAME_MAX 32

typedef struct TlvFieldEntry {
    uint8_t id;
    char    name[TLV_RUNTIME_FIELD_NAME_MAX];
} TlvFieldEntry;

typedef struct TlvFieldMap {
    TlvFieldEntry entries[TLV_RUNTIME_MAX_FIELDS];
    uint8_t       count;
} TlvFieldMap;

/*------------------------------------------------------------------------*/
/* CRC fingerprint map (parsed from type-0x04 block)                     */

#define TLV_RUNTIME_CSV_NAME_MAX 64

typedef struct TlvCrcEntry {
    char     csv_name[TLV_RUNTIME_CSV_NAME_MAX];
    uint32_t crc32;
} TlvCrcEntry;

typedef struct TlvCrcMap {
    TlvCrcEntry  *entries;  /* heap-allocated array                       */
    unsigned long count;
} TlvCrcMap;

/*------------------------------------------------------------------------*/
/* Group map (parsed from type-0x02 block)                               */

#define TLV_RUNTIME_GROUP_NAME_MAX 128

typedef struct TlvGroupEntry {
    uint16_t id;
    char     name[TLV_RUNTIME_GROUP_NAME_MAX];
} TlvGroupEntry;

typedef struct TlvGroupMap {
    TlvGroupEntry *entries;  /* heap-allocated array                       */
    unsigned long  count;
} TlvGroupMap;

/*------------------------------------------------------------------------*/
/* Runtime state                                                          */

typedef struct TlvRuntime {
    TlvReader     reader;           /* owns the file buffer               */
    TlvHeaderInfo header;           /* validated header fields            */
    TlvFieldMap   field_map;        /* field id <-> name table            */
    TlvCrcMap     crc_map;          /* csv fingerprints embedded in TLV   */
    TlvGroupMap   group_map;        /* group-id -> name map (block 0x02)  */
    int           has_field_map;    /* 1 if type-0x01 block was parsed    */
    int           has_crc_map;      /* 1 if type-0x04 block was parsed    */
    int           has_group_map;    /* 1 if type-0x02 block was parsed    */
    uint8_t       group_id_field_id;/* 0 if group_id not in field map     */
    unsigned long data_offset;      /* byte offset where data records begin */
} TlvRuntime;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Initialise to a known-empty state.  Must be called before any other
 * tlv_runtime_* function. */
void tlv_runtime_init(TlvRuntime *rt);

/* Load TLV file and parse all top-level blocks (field map, CRC block).
 * Returns WHD_FILTER_OK or a negative error code. */
int  tlv_runtime_load(TlvRuntime *rt, const char *path);

/* Release all owned memory. */
void tlv_runtime_free(TlvRuntime *rt);

/* Look up a field name in the embedded field map.
 * Returns the numeric field_id (>= 4) or 0 if not found. */
uint8_t tlv_runtime_field_id(const TlvRuntime *rt, const char *field_name);

/* Look up a field name from a field id.
 * Returns the name string or NULL if not found. */
const char *tlv_runtime_field_name(const TlvRuntime *rt, uint8_t field_id);

/* Look up a group name by group_id from the embedded group map.
 * Returns the name string or NULL if not found or no group map present. */
const char *tlv_runtime_group_name(const TlvRuntime *rt, uint16_t group_id);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_RUNTIME_H */
/* End of Text */
