# System Architecture & Creation Pipeline

**Audience:** Contributors and curious users who want to understand how `dat_to_tlv`
works internally — the module boundaries, the data flowing through each stage, and the
design decisions that tie them together.

**Prerequisite:** Read the
[executive overview](../Overview/executive-overview.md) first for the high-level picture.

---

## Module Map

The source tree is split into three layers. Each layer has its own folder and a
matching `include` tree.

### Entry point

| File | Role |
|------|------|
| [tools_src/dat_to_tlv_main.c](../../../tools_src/dat_to_tlv_main.c) | Program entry point. Parses command-line arguments, drives the per-DAT loop, owns benchmark timing, and writes the summary log. |

### Active pipeline (`src/whdtlv/`)

| Subfolder | Module | Role |
|-----------|--------|------|
| `core/` | `dat_parser_minimal.c` | Reads a Logiqx DAT file and returns an array of `DatRomEntry` structs (name, size, CRC). |
| `core/` | `field_registry.c` | Allocates a `FieldRegistry` and populates it from `pack_types.ini`; assigns runtime field IDs. |
| `core/` | `csv_cache.c` | Loads CSV lookup tables into memory; provides token-to-ID lookups; computes per-CSV CRC-32 fingerprints. |
| `core/` | `filename_processor.c` | Orchestrates per-filename decoding: pre-scan, main token scan, and assembly of a `TLV_Record`. |
| `core/` | `tlv_builder.c` | Manages `TLV_Record` and `TLV_Entry` structs; serialises the final TLV binary (header blocks + data records). |
| `core/` | `group_util.c` | Derives canonical group names from display names; used by the builder to assign `group_id` values. |
| `core/` | `error_handling.c` | Defines `ProcessingResult` / `ProcessingError`; centralises error codes used across the pipeline. |
| `core/` | `tlv_profile.c` | Optional profiling counters for the build pass (enabled by `PROFILE=1`). |
| `io/` | `pack_types_loader.c` | Parses `pack_types.ini`; returns an array of `PackType` structs (field list, display name, DAT stem, etc.). |
| `io/` | `writeLog.c` | Thin wrapper around file I/O for the verbose log (enabled with `--max-log`). |
| `platform/` | `platform_io.c` | Wraps `fopen`/`mkdir`/`free` etc. behind `PLATFORM_AMIGA` guards. |
| `platform/` | `platform_string.c` | Portable string helpers (`whd_strcasecmp`, safe copies). |
| `utils/` | `crc32.c` | Computes CRC-32/ISO-HDLC checksums over arbitrary byte buffers. |
| `utils/` | `prettify.c` | Formats numbers (byte counts, timing) for human-readable log output. |

### Staged modules (in repo, NOT compiled)

The following files are present in `src/whdtlv/core/` but are **not** listed in the
Makefile `SRC` variable and are therefore not part of the current build:

| File | Notes |
|------|-------|
| `variant_iterator.c` / `variant_index.c` | Alternative variant traversal API; planned for a future milestone. |
| `active_set.c` | Bit-set abstraction for tracking which variants survive filtering; staged. |
| `slug_util.c` | URL-slug helpers; staged. |

The filtering subsystem (`src/whdtlv/filtering/`) and the public facade
(`whdtlv_filter_facade.c`) **are** compiled — they are covered in
[02-filtering-system.md](02-filtering-system.md).

---

## Active Pipeline — Data Flow

The following steps describe a single DAT file being converted to a TLV output.
The entry point calls `process_dat_file()` in `dat_to_tlv_main.c` for each DAT.

```
dat_to_tlv_main.c
  └─ whdtlv_load_pack_types()        [pack_types_loader.c]
  └─ tlv_session_init()              [tlv_builder.c]
       └─ field_registry_alloc()     [field_registry.c]
       └─ csv_cache load loop        [csv_cache.c]
  └─ parse_dat_entries_minimal()     [dat_parser_minimal.c]
  └─ tlv_session_process_batch()     [tlv_builder.c]
       └─ tlv_process_filename_orchestrator()  [filename_processor.c]
            └─ prescan_and_strip_tokens()
            └─ per-field token scan + csv_cache_lookup_loaded()
            └─ tlv_record_add_entry()
  └─ inject group_id fields          [group_util.c via tlv_builder.c]
  └─ tlv_write_record_with_metadata()  [tlv_builder.c]
  └─ summary log
```

Each stage is described in detail below.

---

## Stage 1 — Identify the Pack Type

`dat_to_tlv_main.c` calls `whdtlv_load_pack_types()` at startup. This reads
`pack_types.ini` and returns an array of `PackType` structs
([src/whdtlv/io/pack_types_loader.c](../../../src/whdtlv/io/pack_types_loader.c)).

Each `PackType` carries:

- A short `dat_name` stem (e.g. `Game`, `Demo`, `Mags`) used to identify which
  pack applies to a given DAT file.
- A `field_list` — the authoritative ordered list of metadata field names that
  the TLV will contain for this pack (e.g. `chipset`, `language`, `disks`).
- Display name and abbreviation for logging.

The main loop calls `find_pack_index_for_dat()` which strips the date suffix from
the DAT filename and case-insensitively matches the stem against each
`PackType.dat_name`. The matched pack type then drives all subsequent stages for
that DAT file.

> **Design principle:** the field list in `pack_types.ini` is the sole definition
> of what the TLV schema contains for a given pack. Adding or removing a field is
> a configuration change, not a code change.

---

## Stage 2 — Build the Field Registry

`tlv_session_init()` calls `field_registry_alloc()` and then
`build_field_registry_from_ini()` to populate a `FieldRegistry`
([src/whdtlv/core/field_registry.c](../../../src/whdtlv/core/field_registry.c)).

### How field IDs are assigned

Field IDs are **not** hardcoded constants. They are assigned sequentially at
runtime from `pack_types.ini`.

Header block tags and data-record field IDs are interpreted in different
contexts and should not be treated as a single shared namespace. Header blocks
currently use block tags `0x01` (field map), `0x02` (group map), and `0x04`
(CSV fingerprints). Block tag `0x03` is reserved for a file-version field
(`TLV_FILE_VERSION`) but is **not yet written** by the builder — the constant
exists in `tlv_builder.h` but has no call site. Inside data records a completely
separate set of field IDs applies:

- `0x04` is the `display_name` record-boundary marker.
- `0x05` is `group_id`.
- Pack-type fields start at `0x06` and are assigned in the order they appear in
  the INI `[Fields]` section.
- `archive_info` is also registered as a dynamic field in the same ID range.
  It is not CSV-backed and does not participate in token matching or variant
  ranking, but it appears in the field map so readers can locate it by name
  rather than assuming a fixed numeric ID.

The key function `field_registry_alloc()` returns a zeroed `FieldRegistry`.
`build_field_registry_from_ini()` (internally `field_registry_add_field_internal`)
walks the INI field list and appends a `FieldDefinition` for each entry, recording
the field name, the corresponding CSV filename, whether the field allows multiple
values per record, and prescan configuration attributes.

The registry is built **fresh every session** — IDs are not persisted between
runs. The finished TLV embeds the complete field map (block `0x01`) so that the
Amiga reader can discover the ID assignments at load time without any hardcoded
knowledge.

---

## Stage 3 — Load the CSV Lookup Tables

`tlv_session_init()` also drives the CSV loading pass
([src/whdtlv/core/csv_cache.c](../../../src/whdtlv/core/csv_cache.c)).

For every field in the active pack type's field list, the corresponding CSV file
from `assets_raw/defs/` is loaded into a `CSVCache` entry inside a
`GlobalCSVManager`. Each `CSVCache` is an open-addressed hash table mapping
lowercase token strings to numeric IDs.

Performance optimisations baked into the cache:

- A 16-bit djb2 fingerprint is stored alongside each entry as a fast pre-filter
  before `strcmp()`.
- `min_entry_len` / `max_entry_len` fields allow length-triage: tokens outside
  that range are rejected without a hash probe.
- `min_token_count` / `max_token_count` track the underscore-delimited token
  count range, used by the prescan to prune candidates cheaply.

### CRC-32 fingerprinting

As each CSV file is read via text-mode `fgets`, `crc32.c` computes a
CRC-32/ISO-HDLC checksum over the line content as returned by `fgets`. On
Windows, text mode silently converts `\r\n` to `\n`, so the embedded CRC is
over LF-only content. The Amiga-side validator (`tlv_crc_validate.c`) opens
the same CSV in text mode and explicitly strips any remaining `\r` before
hashing, ensuring both sides agree regardless of platform. The checksum is
stored in `CSVCache.crc32` and later embedded in the finished TLV as block
`0x04` (see Stage 7). This creates a permanent record of the exact CSV version
used to produce the index.

### CSV alias rows

A single numeric ID may appear on multiple rows in a CSV file. The **first** row
for a given ID is canonical: its token and description win all reverse lookups
(ID → label). Subsequent rows for the same ID are alias rows — their tokens
participate in forward (token → ID) lookup equally, but are invisible to reverse
lookups. Duplicate token strings (case-insensitive) are silently dropped.

---

## Stage 4 — Parse the DAT File

`parse_dat_entries_minimal()` in
[src/whdtlv/core/dat_parser_minimal.c](../../../src/whdtlv/core/dat_parser_minimal.c)
reads the DAT file and extracts every `<rom ... />` element. For each element it
captures:

| Field | Source | Notes |
|-------|--------|-------|
| `name` | `name=` attribute | The archive filename; drives all metadata decoding. |
| `size_bytes` | `size=` attribute | Raw byte count; zeroed and warned if absent/malformed. |
| `crc32` | `crc=` attribute | Hex CRC-32 digest; zeroed and warned if absent/malformed. |

The result is a heap-allocated array of `DatRomEntry` structs. The DAT is not
consulted again after this point — all metadata comes from decoding the `name`
field in Stage 5.

---

## Stage 5 — Decode Each Filename

`tlv_session_process_batch()` iterates the `DatRomEntry` array and calls
`tlv_process_filename_orchestrator()` for each entry
([src/whdtlv/core/filename_processor.c](../../../src/whdtlv/core/filename_processor.c)).

The orchestrator works in two phases:

### Phase A — Prescan

`prescan_and_strip_tokens()` runs first. Certain fields are marked with
`prescan_enabled = true` in the field registry (configured in `pack_types.ini`
under `[FieldAttributes]`). High-priority fields — typically contributor credits
and variant markers — are matched and extracted before the main pass. Matched spans
are optionally removed from the working copy of the filename so that the main pass
does not misidentify them.

Why prescan matters: the WHDLoad naming convention uses `_` as a universal
delimiter. Without prescan, a multi-word contributor credit could consume tokens
that would otherwise resolve to chipset or language codes.

### Phase B — Main token scan

The remaining filename is scanned field by field in the order defined by the pack
type. For each field, `csv_cache_lookup_loaded()` is called with each candidate
token. A match records the numeric ID; no match leaves the field absent from the
record (or applies the CSV default, if one is configured).

**Compact multilanguage tokens** are handled specially: a run of joined two-character
language codes (e.g. `DeEsFrIt`) is tested as a compound token only if every
two-character chunk resolves to a known Language.csv entry. Any unknown chunk
causes the whole token to be rejected — partial matches are not accepted.

### Writing the record

Each resolved field ID and its value are appended to a `TLV_Record` via
`tlv_record_add_entry()`. The `TLV_Record` is a growable array of `TLV_Entry`
structs, each holding a `field_id`, a `length`, and a pointer to the value bytes.

The two archive transport facts from the DAT — `size_bytes` and `crc32` — are
encoded together into an 8-byte `archive_info` payload (size rounded up to KiB as
a big-endian `uint32`, followed by the CRC-32 as a big-endian `uint32`) and
appended as a single field entry.

---

## Stage 6 — Assign Group IDs

After the batch is processed, `tlv_builder.c` makes a second pass over the records
to assign `group_id` values.

`whdtlv_derive_group_name()` in
[src/whdtlv/core/group_util.c](../../../src/whdtlv/core/group_util.c) derives a
canonical group name from the `display_name` field:

1. Scan forward for the first `_v<digit>` pattern (the version marker).
2. The text before that marker is the group name.
3. If no version marker exists, the full `display_name` is used as-is.
4. An empty result (e.g. the name starts with `_v1`) falls back to the full
   `display_name`.

Examples:

| display_name | group_name |
|---|---|
| `AlienBreed2_v1.0_AGA_En` | `AlienBreed2` |
| `ActionFighter` | `ActionFighter` |
| `V10_AGA` | `V10_AGA` (no `_v<digit>` pattern) |

Every record that shares the same canonical group name receives the same integer
`group_id`, assigned sequentially starting at 1. A `group_id` of 0 is the sentinel
for "absent". The mapping of every `group_id` to its group name is stored in the
TLV header block `0x02` (see Stage 7).

---

## Stage 7 — Assemble and Write the TLV

`tlv_write_record_with_metadata()` in
[src/whdtlv/core/tlv_builder.c](../../../src/whdtlv/core/tlv_builder.c) serialises
the complete TLV output. The file structure is:

```
[ Block 0x01 — Field map       ]
[ Block 0x02 — Group map       ]
[ Block 0x04 — CSV fingerprints]
[ Data records ...             ]
```

### Block 0x01 — Field map

Wire format: `[1 byte 0x01][2 bytes LE payload_size]` then for each registered
field: `[1 byte field_id][1 byte name_len][name_len bytes field_name (no NUL)]`.

The two implicit fields are always first: `display_name` = `0x04`,
`group_id` = `0x05`. Pack-type fields follow from `0x06` upward in registration
order. `archive_info` is also present in the map as a dynamic non-scoring field;
readers should locate it by name from block `0x01` rather than assuming a fixed
numeric ID.

The Amiga reader loads this block at startup and uses it to navigate the data
records by ID. The reader does not need field names baked in at compile time —
any field set is self-describing.

### Block 0x02 — Group map

Wire format: `[1 byte 0x02][2 bytes LE payload_size][2 bytes LE group_count]`
then for each group: `[2 bytes BE group_id][1 byte name_len][name_len bytes
group_name (no NUL)]`.

The runtime reads this block once at load time. Grouping variants by integer
comparison is far cheaper than string-matching display names. Old TLV files that
predate block `0x02` are handled gracefully: the runtime falls back to heuristic
group derivation from the `display_name` field.

> **Block 0x03 — File version (reserved, not yet written).** The constant
> `TLV_TYPE_FILE_VERSION` (`0x03`) and `TLV_FILE_VERSION` (`0x0001`) are
> declared in `tlv_builder.h` for a planned file-version block, but the builder
> does not currently write this block. It is documented here so that future
> implementers do not accidentally reuse tag `0x03` for another purpose.

### Block 0x04 — CSV fingerprints

One entry per CSV loaded during the build: the CSV name (string) followed by its
CRC-32 checksum as a little-endian `uint32`. The checksum is computed over
LF-normalised line content (see *CRC-32 fingerprinting* in Stage 3). A
validator can compare these fingerprints against the current CSV files to detect
stale TLV files before they reach the Amiga.

> **Endianness note:** The TLV format uses a mixed-endian convention. All
> structural framing fields (block sizes, payload lengths, token ID values) and
> the block `0x04` CRC values are little-endian. Only `group_id` (block `0x02`
> entries and field `0x05` in data records) and the `archive_info` numeric fields
> (`archive_size_kib` and `archive_crc32`) use explicit big-endian encoding,
> matching the Motorola byte order native to the 68000 CPU. See the endian
> reference table in
> [docs/tlv-filtering-overview.md](../../tlv-filtering-overview.md) for the
> complete field-by-field list.

### Data records

One record per archive variant. Each record is a sequence of TLV entries:

```
[ field_id (1 byte) ][ length (2 bytes LE) ][ value (length bytes) ]
```

The `display_name` entry (`field_id 0x04`) acts as the record boundary marker: the
reader scans forward until it sees `0x04` to find the start of the next record.
Numeric token ID values are stored as 4-byte little-endian `uint32` values.
Free-form string values, such as version numbers, are stored as raw bytes.

---

## Key Function Reference

| Function | File | Purpose |
|----------|------|---------|
| `whdtlv_load_pack_types` | `pack_types_loader.c` | Parse `pack_types.ini`; return `PackType[]` |
| `field_registry_alloc` | `field_registry.c` | Allocate a zeroed `FieldRegistry` |
| `build_field_registry_from_ini` | `field_registry.c` | Populate registry from INI; assign field IDs |
| `tlv_session_init` | `tlv_builder.c` | Load CSVs and registry; prepare the session |
| `parse_dat_entries_minimal` | `dat_parser_minimal.c` | Parse DAT; return `DatRomEntry[]` |
| `tlv_session_process_batch` | `tlv_builder.c` | Drive per-filename processing loop |
| `tlv_process_filename_orchestrator` | `filename_processor.c` | Decode one filename; populate one `TLV_Record` |
| `prescan_and_strip_tokens` | `filename_processor.c` | Extract high-priority fields before main scan |
| `csv_cache_lookup_loaded` | `csv_cache.c` | Token → ID lookup in a loaded `CSVCache` |
| `tlv_record_add_entry` | `tlv_builder.c` | Append one field entry to a `TLV_Record` |
| `whdtlv_derive_group_name` | `group_util.c` | Derive canonical group name from display name |
| `tlv_write_record_with_metadata` | `tlv_builder.c` | Serialise header blocks + data records to file |
| `whdtlv_build_from_dat` | `whdtlv_integration.c` | Public integration entry point: accepts a DAT path and options, runs the full pipeline, and returns a completed TLV in memory. External callers — integration tests, tools, and the filter facade — use this function rather than calling individual pipeline stages directly. See [TLV_INTEGRATION_GUIDE.md](../../../TLV_INTEGRATION_GUIDE.md) for the calling contract. |

---

## Design Principles

**Pack type drives the schema.** The field list in `pack_types.ini` is the only
definition of what the TLV contains for a given pack. Changing what fields are
included is a configuration change, not a code change.

**Field IDs are runtime-assigned, not hardcoded.** The field registry assigns IDs
fresh every session. The TLV embeds the field map (block `0x01`) so any reader can
discover the assignments without prior knowledge. This means new fields can be
added without modifying the Amiga binary.

**Tokens, not strings.** Wherever a field has a finite value set (chipset,
language, media type, etc.), the TLV stores a numeric token ID rather than the
string. This keeps the file small and makes lookups on the Amiga trivially fast.

**All heavy work is on the host.** CSV loading, token resolution, and filename
decoding all happen on the host PC. The Amiga receives a pre-processed binary and
does no parsing at runtime.

**Source data is fingerprinted.** Every CSV lookup table has its CRC-32 stored in
the TLV (block `0x04`). If a CSV is edited after a TLV is built, the checksum will
no longer match. A host-side validator can detect the mismatch before a stale index
reaches the Amiga.

**Variants are pre-grouped.** The builder assigns `group_id` to every variant and
stores the group map in block `0x02`. The runtime groups variants by integer
comparison rather than string parsing. Old TLVs without block `0x02` remain
readable via heuristic fallback.

---

## Further Reading

- [docs/tlv-pipeline-overview.md](../../tlv-pipeline-overview.md) — original
  pipeline reference (do not duplicate; this document builds on it).
- [docs/pack-types-ini-format.md](../../pack-types-ini-format.md) — full INI
  format reference for `pack_types.ini`.
- [docs/prerequisites.md](../../prerequisites.md) — DAT stem extraction rules
  and build prerequisites.
- [02-filtering-system.md](02-filtering-system.md) — how the finished TLV is
  read and filtered at runtime.
