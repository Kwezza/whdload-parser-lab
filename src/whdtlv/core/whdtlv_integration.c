/* src_raw/whdtlv_integration.c - Public facade for the WHDLoad DAT-to-TLV pipeline
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Thin wrapper around the existing session-based pipeline.  Exposes the
 * simple call-once API declared in "whdtlv/whdtlv.h".
 *
 * C89-compatible; vbcc-safe.
 * - Variables declared at block top.
 * - No VLAs, no for-loop init declarations.
 * - No dynamic allocation in this file (delegated to pipeline modules).
 */

#include "whdtlv/whdtlv.h"

#include "platform.h"
#include "whdtlv/platform/platform_io.h"
#include "whdtlv/core/tlv_builder.h"
#include "whdtlv/core/field_registry.h"
#include "whdtlv/io/pack_types_loader.h"
#include "whdtlv/io/writeLog.h"
#include "whdtlv/core/dat_parser_minimal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*------------------------------------------------------------------------*/
/* Internal helpers                                                       */

static void encode_u32_be_facade(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)((val >> 24) & 0xffu);
    dst[1] = (uint8_t)((val >> 16) & 0xffu);
    dst[2] = (uint8_t)((val >>  8) & 0xffu);
    dst[3] = (uint8_t)( val        & 0xffu);
}

/*
 * Encode archive_info 8-byte payload: size_kib (BE) then crc32 (BE).
 * size_kib = (size_bytes + 1023) / 1024  (rounded-up KiB, uint32)
 */
static void encode_archive_info_facade(uint8_t buf[8],
                                       uint32_t size_bytes,
                                       uint32_t crc32_val)
{
    uint32_t size_kib;

    if (size_bytes == 0u) {
        size_kib = 0u;
    } else {
        size_kib = (size_bytes + 1023u) / 1024u;
    }
    encode_u32_be_facade(buf,     size_kib);
    encode_u32_be_facade(buf + 4, crc32_val);
}

static void free_record_array_facade(TLV_Record *records, size_t count)
{
    size_t i;

    if (!records) {
        return;
    }
    for (i = 0u; i < count; i++) {
        if (records[i].entries) {
            tlv_record_free(&records[i]);
        }
    }
    whd_free(records);
}

static bool merge_record_into_aggregate_facade(TLV_Record *aggregate,
                                               const TLV_Record *source)
{
    uint32_t i;

    if (!aggregate || !source) {
        return false;
    }
    for (i = 0u; i < source->entry_count; i++) {
        const TLV_Entry *entry = &source->entries[i];
        if (!tlv_record_add_entry(aggregate,
                                  entry->field_id,
                                  entry->value,
                                  entry->length)) {
            return false;
        }
    }
    return true;
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */

void whdtlv_build_options_defaults(WhdTlvBuildOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    /* All fields zero: logging off, profiling off, reserved zeroed. */
}

int whdtlv_build_from_dat(
    const char                *dat_path,
    const char                *defs_dir,
    const char                *pack_types_path,
    const char                *output_tlv_path,
    unsigned int               pack_type_id,
    const WhdTlvBuildOptions  *options,
    WhdTlvBuildSummary        *summary)
{
    WhdTlvBuildOptions effective_opts;
    DatRomEntry        *dat_entries;
    const char        **name_ptrs;
    size_t              filename_count;
    TLV_Record         *records;
    TLV_Record          aggregate;
    ProcessingSummary   proc_summary;
    FieldRegistry      *field_registry;
    FILE               *output_file;
    PackType           *pack_types;
    size_t              pack_count;
    uint8_t             archive_info_id;
    uint8_t             buf[8];
    size_t              i;
    int                 result;
    bool                session_open;

    /* --- Validate required arguments --- */
    if (!dat_path || !defs_dir || !pack_types_path || !output_tlv_path) {
        return WHDTLV_ERR_INVALID_ARG;
    }

    /* --- Apply options --- */
    whdtlv_build_options_defaults(&effective_opts);
    if (options) {
        effective_opts.enable_logging = options->enable_logging;
        effective_opts.enable_profile = options->enable_profile;
    }

    /* --- Initialise state --- */
    dat_entries    = NULL;
    name_ptrs      = NULL;
    filename_count = 0u;
    records        = NULL;
    field_registry = NULL;
    output_file    = NULL;
    pack_types     = NULL;
    pack_count     = 0u;
    session_open   = false;
    result         = WHDTLV_ERR_PARSE;
    memset(&aggregate,    0, sizeof(aggregate));
    memset(&proc_summary, 0, sizeof(proc_summary));

    /* --- Logging --- */
    whdtlv_log_set_enabled(effective_opts.enable_logging ? true : false);
    whdtlv_log_init();
    whdtlv_log_append("whdtlv_build_from_dat: starting for '%s'", dat_path);

    /* --- Load pack types --- */
    pack_types = whdtlv_load_pack_types(pack_types_path, &pack_count);
    if (!pack_types || pack_count == 0u) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: failed to load pack types from '%s'",
                          pack_types_path);
        result = WHDTLV_ERR_IO;
        goto cleanup;
    }

    /* --- Initialise TLV session --- */
    if (!tlv_session_init(defs_dir, pack_types_path)) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: failed to initialise TLV session");
        result = WHDTLV_ERR_IO;
        goto cleanup;
    }
    session_open = true;

    /* --- Parse DAT entries --- */
    filename_count = parse_dat_entries_minimal(dat_path, &dat_entries);
    if (filename_count == 0u || !dat_entries) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: no DAT entries extracted from '%s'",
                          dat_path);
        result = WHDTLV_ERR_PARSE;
        goto cleanup;
    }

    /* --- Build name pointer array --- */
    name_ptrs = (const char **)whd_malloc(filename_count * sizeof(const char *));
    if (!name_ptrs) {
        result = WHDTLV_ERR_ALLOC;
        goto cleanup;
    }
    for (i = 0u; i < filename_count; i++) {
        name_ptrs[i] = dat_entries[i].name;
    }

    /* --- Allocate per-file record array --- */
    records = (TLV_Record *)whd_malloc(filename_count * sizeof(TLV_Record));
    if (!records) {
        result = WHDTLV_ERR_ALLOC;
        goto cleanup;
    }
    memset(records, 0, filename_count * sizeof(TLV_Record));

    /* --- Process batch --- */
    if (!tlv_session_process_batch(name_ptrs,
                                   (uint32_t)filename_count,
                                   (uint32_t)pack_type_id,
                                   records,
                                   &proc_summary)) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: batch processing failed");
        result = WHDTLV_ERR_PARSE;
        goto cleanup;
    }

    /* --- Build field registry for archive_info injection --- */
    field_registry = field_registry_alloc();
    if (!field_registry || !build_field_registry_from_ini(field_registry, pack_types_path)) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: failed to build field registry");
        result = WHDTLV_ERR_IO;
        goto cleanup;
    }

    /* --- Inject archive_info (size_kib + crc32, big-endian) --- */
    archive_info_id = field_registry_get_id(field_registry, "archive_info");
    if (archive_info_id != 0u) {
        for (i = 0u; i < filename_count; i++) {
            if (records[i].entry_count == 0u) {
                continue;
            }
            encode_archive_info_facade(buf,
                                       dat_entries[i].size_bytes,
                                       dat_entries[i].crc32);
            (void)tlv_record_add_entry(&records[i], archive_info_id, buf, 8u);
        }
    }

    /* --- Inject group_id fields --- */
    (void)tlv_session_inject_group_ids(records, (uint32_t)filename_count);

    /* --- Aggregate records --- */
    if (!tlv_record_init(&aggregate)) {
        result = WHDTLV_ERR_ALLOC;
        goto cleanup;
    }
    for (i = 0u; i < filename_count; i++) {
        if (records[i].entry_count == 0u) {
            continue;
        }
        if (!merge_record_into_aggregate_facade(&aggregate, &records[i])) {
            result = WHDTLV_ERR_ALLOC;
            goto cleanup;
        }
    }

    if (aggregate.entry_count == 0u) {
        result = WHDTLV_ERR_PARSE;
        goto cleanup;
    }

    /* --- Write TLV output --- */
    output_file = whd_fopen(output_tlv_path, "wb");
    if (!output_file) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: failed to open output '%s'",
                          output_tlv_path);
        result = WHDTLV_ERR_IO;
        goto cleanup;
    }
    if (!tlv_write_record_with_metadata(output_file, &aggregate, field_registry)) {
        whdtlv_log_append("whdtlv_build_from_dat: ERROR: failed to write TLV output");
        result = WHDTLV_ERR_IO;
        goto cleanup;
    }
    whd_fclose(output_file);
    output_file = NULL;

    /* --- Fill summary --- */
    if (summary) {
        memset(summary, 0, sizeof(*summary));
        summary->records_written  = (unsigned int)proc_summary.successful_count;
        summary->records_skipped  = (unsigned int)proc_summary.error_count;
        summary->groups_assigned  = 0u; /* group count not exposed by current session API */
    }

    whdtlv_log_append("whdtlv_build_from_dat: complete — %lu records written",
                      (unsigned long)proc_summary.successful_count);
    result = WHDTLV_OK;

cleanup:
    if (output_file) {
        whd_fclose(output_file);
    }
    if (field_registry) {
        field_registry_free(field_registry);
    }
    if (aggregate.entries) {
        tlv_record_free(&aggregate);
    }
    if (records) {
        free_record_array_facade(records, filename_count);
    }
    if (name_ptrs) {
        whd_free(name_ptrs);
    }
    if (dat_entries) {
        free_dat_entries_minimal(dat_entries, filename_count);
    }
    if (session_open) {
        tlv_session_finalize();
    }
    if (pack_types) {
        whdtlv_free_pack_types(pack_types, pack_count);
    }

    return result;
}
