# TLV Creation and Filtering — Integration Guide

This guide explains how to pull the TLV creation and filtering system out of this repository and drop it into a real project. It covers what files you need, what runtime assets are required, how to call the APIs in order, and what each call returns.

---

## Overview

The system has two independent halves:

| Half | Purpose | Status |
|------|---------|--------|
| **TLV Creation** | Convert WHDLoad archive filenames into a binary TLV file | Fully working (host and Amiga) |
| **TLV Filtering** | Load a TLV file, build variant index, score and rank variants by profile | Code present; not yet wired into the shipped build |

They share the same types and headers. You can use creation alone, filtering alone (reading an existing TLV), or both together.

---

## Part 1 — TLV Creation

### 1.1 Files to Copy

**Source files** (`src_raw/`):

```
src_raw/error_handling.c
src_raw/field_registry.c
src_raw/csv_cache.c
src_raw/filename_processor.c
src_raw/tlv_builder.c
src_raw/slug_util.c
src_raw/tlv_profile.c      (only needed if building with PROFILE=1)
```

**Support files** (`src/`):

```
src/platform/platform_io.c
src/platform/platform_string.c
src/io/pack_types_loader.c
src/io/writeLog.c
src/utils/prettify.c
```

**Headers** (copy entire directories):

```
include/               -> your include path
include_raw/           -> your include path
```

**Runtime assets** (must be present at runtime):

```
assets_raw/defs/       -> your definitions folder (all CSV files)
assets_raw/prefs/pack_types.ini
```

The CSV files and `pack_types.ini` are not embedded in the binary. They are read at startup. The relative paths passed to the API calls must resolve to these files at runtime.

### 1.2 Compile Requirements

- C89-compatible compiler (required for Amiga/vbcc; GCC C99 also works on host).
- Define `PLATFORM_AMIGA` when building for Amiga; omit it for host builds.
- Include paths must resolve `<platform.h>`, `<tlv_filename/...>`, `<filter/...>`, and `<io/...>` from your copied header directories.
- No external library dependencies beyond the C standard library.

Minimum compile flags (GCC host example):

```sh
gcc -std=c99 -I./include -I./include_raw \
    src_raw/error_handling.c \
    src_raw/field_registry.c \
    src_raw/csv_cache.c \
    src_raw/filename_processor.c \
    src_raw/tlv_builder.c \
    src_raw/slug_util.c \
    src/platform/platform_io.c \
    src/platform/platform_string.c \
    src/io/pack_types_loader.c \
    src/io/writeLog.c \
    src/utils/prettify.c \
    your_main.c -o your_program
```

### 1.3 Session Lifecycle

TLV creation is session-based. One session processes one batch of filenames and produces one TLV file.

For most callers, the single-call facade is sufficient (see §1.5). The session API below is for
advanced callers that need per-record access.

```
whdtlv_session_init()
  -> whdtlv_session_process_batch()
  -> whdtlv_session_inject_group_ids()   (optional, adds group clustering)
  -> merge records into aggregate
  -> write TLV file
whdtlv_session_finalize()
```

### 1.4 Step-by-Step API Calls

#### Step 1 — Initialise the session

```c
#include <tlv_filename/tlv_builder.h>

bool ok = whdtlv_session_init(
    "path/to/defs",          /* folder containing all CSV files */
    "path/to/pack_types.ini" /* pack type configuration */
);
/* Returns: true on success. false if CSV folder or INI file cannot be loaded. */
```

This call loads `pack_types.ini`, builds the dynamic field registry, and pre-loads the CSV lookup tables. It must succeed before anything else can run.

#### Step 2 — Process a batch of filenames

```c
#include <tlv_filename/tlv_builder.h>

const char *filenames[] = {
    "Lemmings(AGA)(En)(CD32).lha",
    "Lemmings(OCS)(En)(2Disk).lha",
    /* ... */
};
uint32_t count = 2;

TLV_Record *records = calloc(count, sizeof(TLV_Record));

ProcessingSummary summary;
bool ok = whdtlv_session_process_batch(
    filenames,   /* array of raw archive filenames */
    count,       /* number of filenames */
    1,           /* pack_type_id — matches an id in pack_types.ini (1=Games, 2=Demos, etc.) */
    records,     /* caller-allocated array of TLV_Record, one per filename */
    &summary     /* receives totals on return */
);
/*
 * Returns: true if the batch ran without a fatal error.
 *          Individual per-filename failures are counted in summary.error_count
 *          but do not make the overall call return false.
 *
 * summary.total_processed  — number of filenames attempted
 * summary.successful_count — number that produced a valid record
 * summary.error_count      — number that failed (record will be empty)
 */
```

Each `TLV_Record` in `records` is owned by the caller after this call. You must free it with `tlv_record_free()` when done.

#### Step 3 — (Optional) Inject group IDs

Group IDs cluster variant records that share the same canonical game title. This is needed if your reader needs to group variants (e.g. all editions of "Lemmings" under one group_id).

```c
bool ok = whdtlv_session_inject_group_ids(records, count);
/*
 * Returns: true on success.
 *          Appends a group_id TLV entry to each record.
 *          Must be called before merging records into the aggregate.
 */
```

#### Step 4 — Build an aggregate record and write the TLV file

```c
TLV_Record aggregate;
tlv_record_init(&aggregate);

for (uint32_t i = 0; i < count; i++) {
    /* merge each per-filename record into aggregate */
    for (uint32_t j = 0; j < records[i].entry_count; j++) {
        const TLV_Entry *e = &records[i].entries[j];
        tlv_record_add_entry(&aggregate, e->field_id, e->value, e->length);
    }
}

FILE *out = fopen("output/MyGames.tlv", "wb");
/* Writes metadata map (block 0x01), group map (block 0x02),
 * file version (block 0x03), then all variant data records. */
bool ok = whdtlv_write_record(out, &aggregate, /* field_registry is internal */);
fclose(out);

tlv_record_free(&aggregate);
```

> **Note:** The field registry is held internally by the session. `tlv_write_record_with_metadata` accesses it through the active session context. You do not pass it directly to the write call from the public API.

#### Step 5 — Finalise

```c
whdtlv_session_finalize();
/* Frees all session-internal resources (registry, CSV caches, group map). */
```

### 1.5 High-Level Convenience Function

For a simple single-DAT conversion without step-by-step control, use the public facade. Include
only this one header — no internal pipeline headers are needed:

```c
#include <integration/whdtlv_integration.h>

WhdTlvBuildOptions opts;
WhdTlvBuildSummary summary;
whdtlv_build_options_defaults(&opts);

int rc = whdtlv_build_from_dat(
    "path/to/input.dat",      /* Logiqx DAT XML file */
    "path/to/defs",           /* CSV definitions folder */
    "path/to/pack_types.ini", /* pack type configuration */
    "path/to/output.tlv",     /* TLV output path */
    0,                        /* pack_type_id: 0 = all types */
    &opts,
    &summary
);
/*
 * Returns: WHDTLV_OK (0) on success, or a WHDTLV_ERR_* code on failure.
 *
 * summary.records_written  — number of records written to the TLV file
 * summary.records_skipped  — number of filenames that produced no record
 * summary.groups_assigned  — number of group IDs injected
 */
```

This is the lowest-friction entry point. Use it when you do not need per-record access.

### 1.6 Error Types

All per-operation errors are reported through `ProcessingResult` and `ProcessingError`:

```c
typedef enum {
    PROCESSING_SUCCESS = 0,
    PROCESSING_WARNING_PARTIAL,
    PROCESSING_ERROR_INVALID_INPUT,
    PROCESSING_ERROR_MEMORY_ALLOCATION,
    PROCESSING_ERROR_CSV_LOOKUP_FAILED,
    PROCESSING_ERROR_TOKEN_NOT_FOUND,
    PROCESSING_ERROR_INVALID_FORMAT,
    PROCESSING_ERROR_TLV_RECORD_ADDITION
} ProcessingResult;
```

`ProcessingError` carries a human-readable `error_message[256]`, the `failed_token[64]` that triggered the failure, the `source_filename[128]`, and the `module_name` that reported it.

---

## Part 2 — TLV Filtering (Variant Scoring)

> **Status:** All modules are present and headers are complete. This subsystem is not compiled into the current `Makefile` build. To use it you must add `src_raw/variant_iterator.c`, `src_raw/variant_index.c`, `src_raw/active_set.c`, `src_raw/filter_profile.c`, `src_raw/filter_pipeline.c`, `src_raw/filter_runtime.c`, and `src_raw/profile_loader.c` to your build.

### 2.1 Additional Files to Add

```
src_raw/variant_iterator.c
src_raw/variant_index.c
src_raw/active_set.c
src_raw/filter_profile.c
src_raw/filter_pipeline.c
src_raw/filter_runtime.c
src_raw/profile_loader.c
```

Headers are already in `include_raw/filter/`.

### 2.2 High-Level Runtime Facade (`FilterRuntime`)

The simplest path to filtering is through `FilterRuntime` in `include_raw/filter/filter_runtime.h`. It owns the registry, CSV cache, and active profile in one struct.

```c
#include <filter/filter_runtime.h>

FilterRuntime rt;

/* Step 1 — initialise */
bool ok = filter_runtime_init(
    &rt,
    "path/to/defs",           /* CSV definitions folder */
    "path/to/pack_types.ini"  /* pack type configuration */
);
/*
 * Returns: true on success.
 *          Allocates the field registry and loads CSVs.
 */

/* Step 2 — build scoring profile */
ok = filter_runtime_build_profile(
    &rt,
    "aga",      /* chipset_include  — comma-separated token strings, or NULL */
    NULL,       /* chipset_exclude */
    "en",       /* language_include */
    NULL,       /* language_exclude */
    "512kb",    /* memory_include */
    NULL,       /* memory_exclude */
    false       /* debug — true emits verbose diagnostics */
);
/*
 * Returns: true if the profile was built from those constraints.
 *          Tokens are matched against the loaded CSV tables.
 */

/* Step 3 — load a TLV snapshot and build the variant index */
TLV_Record record;
TLV_VariantIndex vindex;

ok = filter_runtime_load_snapshot(
    &rt,
    "output/MyGames.tlv",  /* TLV file produced by the creation step */
    &record,               /* receives loaded TLV record */
    &vindex                /* receives built variant index */
);
/*
 * Returns: true on success.
 *          record and vindex are populated.
 *          record must be freed with tlv_record_free() when done.
 *          vindex must be freed with variant_index_free() when done.
 */

/* Step 4 — score all variants */
uint32_t result_count = filter_runtime_score_all(
    &rt,
    &vindex,
    10          /* top_n — return the top N variants by score (0 = return all) */
);
/*
 * Returns: number of active (non-rejected) variants after scoring.
 *          Scores are attached to the internal active set.
 *          Use the FilterPipeline API (see below) to retrieve the ordered list.
 */

/* Step 5 — shutdown */
filter_runtime_shutdown(&rt);
variant_index_free(&vindex);
tlv_record_free(&record);
```

### 2.3 Lower-Level Pipeline (`FilterPipeline`)

Use `FilterPipeline` directly when you need finer control over indexing, active-set filtering, and result retrieval.

```c
#include <filter/filter_pipeline.h>

FilterPipeline fp;
filter_pipeline_init(&fp);

/* Build variant index from a loaded TLV record */
uint32_t variant_count = filter_pipeline_build(
    &fp,
    &record,        /* TLV_Record loaded from file */
    registry        /* FieldRegistry — from FilterRuntime.registry or manually built */
);
/*
 * Returns: number of variants indexed, or 0 on structural error.
 */

/* Apply a pre-built profile to score and rank */
bool ok = filter_pipeline_apply_profile(&fp, &profile);
/*
 * Returns: true on success.
 *          Variants that match exclude rules are deselected from the active set.
 */

/* Retrieve ordered results */
uint32_t out_count;
const uint32_t *sorted = filter_pipeline_get_sorted(&fp, &out_count);
/*
 * Returns: pointer to internal array of variant indices, sorted descending by score.
 *          out_count receives the number of elements.
 *          Pointer is valid until filter_pipeline_free() is called.
 */

/* Apply a field-based structural filter (include/exclude by CSV token string) */
uint32_t remaining = filter_pipeline_apply_field_filter(
    &fp,
    registry,
    csv_manager,
    "chipset",      /* field name from pack_types.ini */
    "aga,cd32",     /* include_list — comma-separated, or NULL to accept all */
    "ocs"           /* exclude_list — comma-separated, or NULL to exclude none */
);
/*
 * Returns: number of active variants remaining after the filter.
 */

/* Text search across active variants */
uint32_t result_indices[64];
uint32_t found = filter_pipeline_apply_search(
    &fp,
    "lemmings",     /* search text */
    result_indices, /* caller-allocated output array */
    64              /* max results */
);
/*
 * Returns: number of matching variant indices written to result_indices.
 */

filter_pipeline_free(&fp);
```

### 2.4 Variant Descriptor Fields

Each variant in `TLV_VariantIndex.items` is a `TLV_VariantDescriptor`:

| Field | Type | Description |
|-------|------|-------------|
| `display_name` | `char *` | Canonical display title (owned, NUL-terminated) |
| `display_len` | `uint32_t` | Length of `display_name` excluding NUL |
| `base_name` | `char[128]` | Normalized name with version/build suffixes stripped |
| `name_hash` | `uint64_t` | FNV-1a hash of `display_name` (lowercase) |
| `base_hash` | `uint64_t` | FNV-1a hash of `base_name` (lowercase) |
| `tokens[]` | struct array | Up to 16 captured field tokens per variant |
| `token_count` | `uint8_t` | Number of filled token slots |
| `start_entry` | `uint32_t` | Index into `TLV_Record.entries` for this variant |
| `entry_count` | `uint16_t` | Total TLV entries belonging to this variant |
| `interior_fields` | `uint16_t` | `entry_count - 1` (the display_name boundary is not counted) |

Each `tokens[i]` has:

| Sub-field | Type | Description |
|-----------|------|-------------|
| `field_id` | `uint8_t` | Runtime field ID from the registry |
| `value` | `const char *` | Pointer into TLV entry value (not owned) |
| `length` | `uint16_t` | Byte length of value |
| `csv_id` | `uint32_t` | Resolved numeric ID from the backing CSV (0 if none) |

---

## Part 3 — TLV Binary Format Reference

The binary layout written by `tlv_write_record_with_metadata` is:

```
[Block 0x01] Metadata map   — field_id -> field_name mapping + CSV CRC fingerprints
[Block 0x02] Group map      — group_id (uint16 BE) -> group_name string
[Block 0x03] File version   — uint16 LE format version (current: 0x0001)
[Block 0x04+] Variant data  — one TLV entry per field per variant
```

Each entry on disk:

```
[1 byte ] field_id
[2 bytes] value_length (little-endian)
[N bytes] value data
```

Reserved type IDs:

| ID | Meaning |
|----|---------|
| `0x01` | Metadata map |
| `0x02` | Group map |
| `0x03` | File version |
| `0x04`–`0xFF` | Dynamic field data (assigned by the registry at session start) |

Because field IDs are assigned dynamically each session, the embedded metadata map (block 0x01) is the authoritative mapping. Always read the metadata map before interpreting field data. `tlv_read_record_with_metadata()` does this automatically when loading a file.

---

## Part 4 — Runtime Asset Layout

Your deployed project must provide these files at the paths you pass to `whdtlv_build_from_dat` (or `whdtlv_session_init` for the advanced API) and `filter_runtime_init`:

```
defs/
    Chipset.csv
    Language.csv
    Media.csv
    Memory.csv
    Video.csv
    Disks.csv
    contributors.csv
    crack_groups.csv
    software_houses.csv
    compilations.csv
    cover_disks.csv
    demo_groups.csv
    magazines.csv
    variant_tags.csv
    filename.csv
    name_overrides.csv
    special_overrides.csv
    Special.csv
prefs/
    pack_types.ini
```

CSV files are plain-text token-to-ID tables. The format and the expected filenames are determined by the field list entries in `pack_types.ini`. If a CSV file referenced by a pack type field is missing, the session logs a warning and continues; that field will produce no matches for entries relying on it.

---

## Part 5 — Known Issues

- **Amiga runtime crash:** The Amiga binary currently crashes at runtime during TLV creation. Host builds work correctly. Raise the stack before running on real or emulated hardware: `STACK 100000`.
- **Filter modules not in default build:** `filter_pipeline.c`, `filter_runtime.c`, `variant_index.c`, `active_set.c`, `filter_profile.c`, `profile_loader.c`, and `variant_iterator.c` are present but not compiled by the current `Makefile`. Add them manually to your build system.
- **Field IDs are session-local:** Do not hardcode numeric field IDs. They are assigned dynamically per session. Always use the registry API (`field_registry_get_id`) or the embedded metadata map in the TLV file to resolve names to IDs.
