# Deep Dive 2 — The Filtering System

> **Audience:** Contributors and technically curious users.  
> Assumes you have already read [01-architecture-and-creation-pipeline.md](01-architecture-and-creation-pipeline.md)
> and the [TLV filtering overview](../../tlv-filtering-overview.md).  
> Do not repeat content from those documents — links are provided instead.

---

## Overview

Filtering is deliberately separated from creation. Once a TLV index has been
built, the filtering subsystem can select the best variant for your machine
using **only the TLV file and the CSV definition files** — it never re-reads
the original DAT or re-runs the tokeniser. The TLV is self-describing: every
field name and its runtime ID are embedded in a header block, so the filter
can reconstruct the full field registry without any external configuration.

The full pipeline, in order:

| Stage | Module(s) | What happens |
|-------|-----------|-------------|
| 1 | `tlv_reader.c`, `tlv_crc_validate.c` | Load TLV into memory; parse header blocks; validate CSV fingerprints |
| 2 | `tlv_variant.c`, `tlv_group.c` | Reconstruct per-variant views; group variants by game title |
| 3 | `tlv_runtime.c`, `profile_binder.c` | Build in-memory field registry from embedded map; resolve profile tokens to numeric IDs |
| 4 | `selection_plan.c` | Compute the Cartesian lane plan from slash-separated buckets |
| 5 | `tlv_filter.c`, `tlv_select.c` | Score each variant; reject excluded ones; pick winner per lane per group |
| 6 | `tlv_results.c` | Collect winners into an in-memory list or write to a text file |
| Search | `whd_search.c` | Optional group-level pre-filter: restrict scoring to groups whose canonical name matches a search pattern |
| Public API | `whdtlv_filter_facade.c` | Single entry point wrapping the entire pipeline |

---

## Stage 1 — Load and Validate

### `tlv_reader.c` — raw load

`tlv_reader_load()` opens the TLV file, reads its entire content into a
heap buffer, and scans the leading header blocks to find the boundary where
data records begin (`data_offset`).

The first block in a well-formed TLV **must** be type `0x01` (the field
metadata map).  If any other byte appears first the load fails with
`WHD_FILTER_ERR_TLV_HEADER`.  The TLV format uses a **mixed-endian** convention:
structural framing fields (block payload sizes, record value lengths, CRC
fingerprint values, and CSV-backed token IDs) are little-endian, while the
Amiga-direct payloads `group_id` and the numeric subfields inside
`archive_info` (`archive_size_kib`, `archive_crc32`) are big-endian (Motorola
byte order, native to the 68000 CPU).  The reader helpers `u16_le()` and
`u32_le()` in [tlv_runtime.c](../../../src/whdtlv/filtering/tlv_runtime.c)
handle the framing fields; `group_id` is read with an explicit
`(buf[pos] << 8) | buf[pos+1]` shift in
[tlv_variant.c](../../../src/whdtlv/filtering/tlv_variant.c).

**Endian quick-reference** (see [tlv-filtering-overview.md](../../tlv-filtering-overview.md) for the full field-by-field table):

| Field / region | Byte order | Where read |
|----------------|-----------|------------|
| Block payload sizes | LE | `tlv_runtime.c` `u16_le()` |
| Record value lengths | LE | `tlv_runtime.c` `u16_le()` |
| CRC-32 fingerprints (block `0x04`) | LE | `tlv_runtime.c` `u32_le()` |
| CSV-backed token IDs | LE `uint32` | `tlv_select.c` `read_u32_le()` |
| `group_id` (field `0x05`) | **BE** `uint16` | `tlv_variant.c` shift read |
| `archive_size_kib`, `archive_crc32` | **BE** `uint32` | `dat_to_tlv_main.c` write helper |

Header block type bytes:

| Byte | Block | Content |
|------|-------|---------|
| `0x01` | Field metadata map | Sequence of `(field_id, NUL-terminated name)` pairs |
| `0x02` | Group map | Optional `group_id` → canonical group-name table; absent in old TLVs, which fall back gracefully |
| `0x04` | CSV fingerprints | `(csv_name, CRC-32)` pairs — one per field that has a lookup table |

After scanning the header the reader records `data_offset` — the byte
position where the first data record begins.  Every byte at or beyond
`data_offset` is a variant field entry; the reader never interprets them
during the load phase.

### `tlv_crc_validate.c` — integrity check

Before any filtering runs, `tlv_crc_validate()` iterates over the CSV
fingerprints embedded in block `0x04`.  For each entry it:

1. Builds the path `<defs_dir>/<csv_name>.csv`.
2. Reads the file in text mode (matching the mode used during creation,
   so `\r\n` → `\n` translation is applied consistently on Windows).
3. Computes a CRC-32/ISO-HDLC checksum via [`crc32.c`](../../../src/whdtlv/utils/crc32.c).
4. Compares the result against the stored value.

Two modes are supported via the `flags` field in `WhdFilterRequest`:

- **`WHD_FILTER_CRC_STRICT`** — any mismatch aborts with
  `WHD_FILTER_ERR_CSV_CRC_MISMATCH`.
- **`WHD_FILTER_CRC_WARNONLY`** — mismatches are counted in
  `WhdCrcValidateResult.mismatch_count` and execution continues.  The caller
  can inspect `result->crc_mismatch_count` after `whd_filter_run()` returns.

This check guards against running a TLV built from one version of the CSV
tables against a different (modified) version, which would silently produce
wrong token IDs.  If a CSV has changed since the TLV was created, the CRC
will not match and the filter will either abort (strict mode) or warn; in
either case the TLV must be **rebuilt** from the updated CSV files before
filtering can produce correct results.

---

## Stage 2 — Variant Views and Grouping

### `tlv_variant.c` — reconstructing variants

`tlv_variant_build_array()` walks the data region of the TLV buffer from
`data_offset` to the end, entry by entry.  Each entry has the wire layout:

```
[1 byte]  field_id
[2 bytes] value length (LE uint16)
[N bytes] value bytes
```

A new variant begins every time the `display_field_id` appears — this is the
field whose value holds the sanitised archive filename (no extension).  The
function populates a `WhdVariantView` for each variant:

| Field | Source |
|-------|--------|
| `filename` | Pointer into the TLV buffer at the display-field value bytes |
| `base_name` | Canonical group name derived by `whdtlv_derive_group_name()` from the filename |
| `group_id` | 2-byte big-endian payload of the `group_id` field entry, or `0` if absent |
| `original_index` | 0-based scan order (runtime-only, never stored in TLV) |
| `field_data[]` | Array of `(field_id, value_ptr, value_len)` triples for all other fields |

CSV-backed field values are stored as 4-byte little-endian `uint32` IDs in
the TLV.  The scoring stage reads them back with a `read_u32_le()` helper.

### `tlv_group.c` — grouping into game titles

`tlv_group_build()` sorts the variant array and identifies group boundaries.
Two paths are supported:

- **`group_id` path** (used when the TLV field map contains `group_id`):
  sort by `(group_id ASC, original_index ASC)`.  Boundary = change in
  `group_id`.
- **Fallback path** (old TLVs without `group_id`):
  sort by `(base_name ASC, original_index ASC)`.  Boundary = change in
  `base_name`.

Within a group, `original_index` preserves TLV scan order so that the
first-encountered variant wins any score tie — a predictable, stable
tie-break rule.

---

## Stage 3 — Runtime Initialisation

### `tlv_runtime.c` — building the in-memory field registry

`tlv_runtime_load()` calls `tlv_reader_load()` and then parses three header
blocks in order:

- **Block `0x01`** — field metadata map: builds an in-memory table of
  `(field_id → field_name)` pairs.
- **Block `0x02`** — group map (optional): maps each numeric `group_id` to its
  canonical group name and sets `rt->has_group_map`.  Old TLVs that pre-date
  this block skip it gracefully; the runtime falls back to string-based grouping.
- **Block `0x04`** — CSV fingerprints: records `(csv_name → stored_crc)` pairs
  so that `tlv_crc_validate()` can compare them against the live CSV files.

Critically, **no external INI or field-registry file is needed**: the TLV
carries its own schema.  This means a TLV built on a development machine can
be used on an Amiga without shipping `pack_types.ini` alongside it.

### `profile_binder.c` — token resolution

`profile_binder_load()` parses a `.profile` INI file and resolves every
filter token to the numeric ID that the TLV uses at runtime.

Token resolution order (per field):

1. **CSV lookup** — scan the TLV's CRC map for a CSV whose base name
   matches the field name (case-insensitive).  If found, open the CSV and
   search for the token string in column 2.  Return the numeric ID from
   column 1.
2. **FNV-1a 8-bit hash fallback** — if no CSV is found or the token is
   absent from the CSV, compute `fnv1a_8bit(lowercase(token))`.  The TLV
   builder uses the same fallback, so hash-produced IDs compare equal at
   scoring time.

The fallback means **unrecognised tokens never crash the filter**; they are
bound using the hash ID and will match any TLV record that was hashed the
same way at build time.  The hash fallback is silent — no warning is emitted
for a token absent from its CSV.  A warning (`had_warnings = 1` on the
`WhdBoundProfile`) is set only when an entire `[Filter.<field>]` section
refers to a field that does not exist in the TLV's embedded field map.

A `WhdBoundProfile` stores, per field:

| Member | Meaning |
|--------|---------|
| `field_id` | TLV field ID from the embedded map |
| `include_ids[]` | Resolved IDs for the `include=` list, in priority order |
| `include_count` | Length of `include_ids[]` |
| `exclude_ids[]` | Resolved IDs for the `exclude=` list |
| `exclude_count` | Length of `exclude_ids[]` |
| `weight` | Scoring weight from `[Scoring]` section |
| `bucket_count` | Number of slash-separated buckets (≥ 1) |
| `rank_by_id[]` | 256-entry lookup: `rank_by_id[id & 0xFF]` = rank (0 = highest priority), `0xFF` = not in include list |

---

## Stage 4 — Selection Plan

`whd_build_selection_plan()` in [selection_plan.c](../../../src/whdtlv/filtering/selection_plan.c)
converts the `WhdBoundProfile` into a `WhdSelectionPlan` that describes how
many independent selection *lanes* the filter should run.

### The slash-bucket mechanism

A comma-separated include list defines one lane:

```
include=AGA,ECS,OCS
```

One best variant per group is selected from all AGA, ECS, and OCS variants.

A slash `/` divides the list into *buckets*, each producing an independent lane:

```
include=AGA/ECS,OCS
```

- Lane 0 selects the best AGA variant per group.
- Lane 1 selects the best ECS or OCS variant per group.

Up to one variant per group is selected per lane, so a game with both AGA
and OCS variants would appear **twice** in the output.  This is the mechanism
used to build a collection that covers multiple chipset families simultaneously.

See [multi_bucket_reference.profile](../../../assets_raw/profiles/multi_bucket_reference.profile)
for a heavily-commented reference profile demonstrating this feature.

### Lane construction

`whd_build_selection_plan()` identifies all *slash-enabled* fields (those
with `bucket_count > 1`) and builds lanes as the Cartesian product of their
bucket indices using iterative modulo/division indexing — no recursion, no
heap beyond the plan struct itself.  Hard caps prevent combinatorial
explosion:

- `FP_MAX_BUCKET_FIELDS` = **4** — maximum slash-enabled fields per profile
- `FP_MAX_BUCKETS_FIELD` = **8** — maximum buckets in a single include list
- `FP_MAX_SELECTION_LANES` = **32** — maximum generated lanes after Cartesian expansion

Exceeding any of these limits **rejects the profile** at load time with a clear
error; lanes are never silently truncated.

---

## Stage 5 — Scoring and Selection

### `tlv_select.c` — per-variant scoring

`tlv_select_run()` iterates over every group and, for each lane, scores all
variants in that group.

**Scoring rules** (see also [docs/profile_system.md](../../profile_system.md)):

1. **Exclude check first** — if any field value for a variant matches an
   `exclude_ids[]` entry, the variant is immediately rejected for all lanes.
2. **Field score** — for each included field:
   - `rank = rank_by_id[token_id & 0xFF]`  (0 = top priority, `0xFF` = not in list)
   - `field_score = (include_count - rank) * weight`
   - An empty include list (zero tokens) accepts all variants but scores 0.
3. **Multi-value fields** — a variant may carry multiple entries for the
   same field ID.  Any excluded value causes rejection.  The **best**
   (highest) field score across all values for the field is taken.
4. **Interior fields** — the variant's `interior_fields` count (the number of
   non-display, non-`group_id` fields carried in the record) is added as a
   small unconditional bonus, favouring variants that encode more decoded
   metadata.  `group_id` is explicitly excluded from this count.
5. **Tie-break** — when two variants have equal total scores, the one with
   the lower `original_index` (first in TLV scan order) wins.

### Per-lane selection

Selection runs in two phases per group:

1. **Rejection pre-pass.** Every variant is evaluated for exclude rules once.
   Any variant excluded by any field is marked rejected and never reconsidered
   by any lane.

2. **Per-lane selection.** For each lane the scorer scans all non-rejected
   variants and checks two additional conditions:

   - **Lane eligibility** — the variant must have an effective token that falls
     inside every bucket required by that lane.  The effective token may be an
     explicit value from the TLV record or the field's CSV default token when
     the variant carries no value for that field at all (e.g. a chipset-less
     variant may match an OCS bucket via the CSV default).  A lane with no
     requirements — the implicit single-lane case when no field uses `/` — accepts
     every non-rejected variant.

   - **Bucket-local rank** — for slash-enabled fields, rank is computed relative
     to the bucket's own token list, not the full include list.  In
     `include=AGA/ECS,OCS`, ECS is rank 0 inside lane 1 and OCS is rank 1, so
     ECS wins within that lane even though its global position in the include
     string is lower than AGA.  Fields without a lane requirement are scored
     using global ranks as normal.

   - **Duplicate suppression** — a variant already selected by an earlier lane
     of the same group is skipped by later lanes.  Each variant can appear at
     most once per group across all lanes.

   The highest-scoring eligible, non-duplicate variant wins the lane.  Ties
   break by lowest `original_index` (first in TLV scan order).

### `tlv_filter.c` — pipeline orchestrator

`whd_filter_run()` is the internal function that wires stages 1–6 together.
It owns the lifetimes of all intermediate structures (`TlvRuntime`,
`WhdVariantArray`, `WhdGroupSet`, `WhdSelectResult`) and frees them before
returning, whether the run succeeds or fails.

The function also handles the optional search pre-filter: if
`request->search_term` is non-NULL and non-empty, `whd_search_build_group_allow_list()`
runs before scoring to produce a `WhdGroupAllowList` that restricts scoring
to groups whose canonical name matches the search pattern.

---

## Stage 6 — Results

`tlv_results_write_file()` and `tlv_results_collect_list()` in
[tlv_results.c](../../../src/whdtlv/filtering/tlv_results.c) iterate over
the `WhdSelectResult` and emit the winners.

Output format: **one selected archive filename per line**, no header.
Rejected groups (all variants excluded) and groups with no selection are
silently skipped.

- `tlv_results_write_file()` — writes directly to a file path.
- `tlv_results_collect_list()` — allocates and returns a
  `WhdTlvStringList` (used by the public facade for in-memory callers).

---

## Search — `whd_search.c`

Search runs as an optional **group-level** pre-filter before scoring.  If
`request->search_term` is non-NULL and non-empty,
`whd_search_build_group_allow_list()` matches the pattern against each
group's canonical name.  New TLVs use the canonical name from block `0x02`
(via `tlv_runtime_group_name()`); older TLVs without the group map fall back
to the `base_name` derived from `display_name`.  The scorer then skips
groups absent from the resulting allow list entirely.

`group_id` values are never modified by search — the allow list is a
temporary runtime mask, not a structural change to the TLV.

Two matching modes:

| Mode | Trigger | Behaviour |
|------|---------|-----------|
| Substring | Pattern contains no `*` or `?` | Case-insensitive `strstr` — `"lotus"` matches `"Lotus2"` |
| Wildcard | Pattern contains `*` or `?` | Iterative backtracking — `*` = zero or more chars, `?` = exactly one char |

ASCII case-folding is used throughout; no locale functions are called, so
the search works identically on host and Amiga.

---

## Public Facade — `whdtlv_filter_facade.c`

[whdtlv_filter_facade.c](../../../src/whdtlv/whdtlv_filter_facade.c)
provides the single entry point that external callers use.  It exposes four
functions (declared in `include/whdtlv/whdtlv.h`):

| Function | Purpose |
|----------|---------|
| `whdtlv_filter_options_defaults()` | Fill a `WhdTlvFilterOptions` struct with safe defaults |
| `whdtlv_filter_to_list()` | Run the full pipeline; return an allocated `WhdTlvStringList` |
| `whdtlv_filter_to_file()` | Convenience wrapper: run pipeline and write results to a file |
| `whdtlv_string_list_free()` | Free memory owned by a list returned by `whdtlv_filter_to_list()` |

The facade translates between the public `WHDTLV_*` error codes and the
internal `WHD_FILTER_ERR_*` codes.  No internal structs are exposed to the
caller; everything the caller needs is in `include/whdtlv/whdtlv.h`.

For a complete usage example and error-code reference see
[docs/whdtlv_public_filter_facade.md](../../whdtlv_public_filter_facade.md).

---

## Profile File Format

A `.profile` file is a plain INI text file.  Full format documentation is in
[docs/profile_system.md](../../profile_system.md).  A brief structural
summary:

```ini
[Profile]
id=pal_aga_4mb
name=PAL AGA 4MB Default
version=1

[Filter.chipset]
include=AGA,ECS,OCS
exclude=CD32,CDTV

[Filter.language]
include=EN,DE
exclude=

[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
```

Key rules:

- Section name `[Filter.<field>]` must match a field name that exists in the TLV's embedded field map.  An unrecognised section is **silently skipped** — no crash, no error.
- `include=` and `exclude=` accept comma-separated token strings.  Slash `/` creates selection buckets (see Stage 4 above).
- `[Scoring]` `weight.<field>=N` — integer weight; `0` disables score contribution while exclusions still apply.
- Missing fields use the CSV `default` column value if one is defined.

---

## Built-in Profiles

| File | Purpose |
|------|---------|
| [Default.profile](../../../assets_raw/profiles/Default.profile) | Baseline profile; applies no meaningful filtering — variants remain eligible and normal per-group selection still chooses the winner |
| [pal_aga_4mb.profile](../../../assets_raw/profiles/pal_aga_4mb.profile) | PAL AGA machine with 4 MB fast RAM; prefers AGA → ECS → OCS, English or German |
| [chipset_aga_only.profile](../../../assets_raw/profiles/chipset_aga_only.profile) | Simplified AGA-first; no language or memory weighting |
| [chipset_legacy_only.profile](../../../assets_raw/profiles/chipset_legacy_only.profile) | OCS/ECS only; excludes AGA variants |
| [multi_bucket_reference.profile](../../../assets_raw/profiles/multi_bucket_reference.profile) | Reference profile demonstrating slash-bucket multi-lane selection |

---

## Staged / Legacy Modules

The following modules are present in the repository but are **not compiled
by the current Makefile**:

> **Status: Staged — present in repo but not compiled by the current Makefile.**

| Module | Notes |
|--------|-------|
| `filter_profile.c` | Alternate profile model; superseded by `profile_binder.c` |
| `filter_pipeline.c` | Alternate pipeline orchestrator; superseded by `tlv_filter.c` |
| `filter_runtime.c` | Alternate runtime; superseded by `tlv_runtime.c` |
| `profile_loader.c` | Earlier profile loader; superseded by `profile_binder.c` |
| `variant_iterator.c` | Iterator abstraction over the variant array; not yet wired in |
| `variant_index.c` | Sorted index helper; not yet wired in |
| `active_set.c` | Active-variant bitset; not yet wired in |

Do not describe any of these as current runtime behaviour.  The active code
path runs exclusively through the modules listed in the pipeline table at the
top of this document.

---

## Key Function Reference

| Function | Module | Purpose |
|----------|--------|---------|
| `tlv_reader_load()` | `tlv_reader.c` | Open TLV file, load into buffer, scan header blocks, record `data_offset` |
| `tlv_runtime_load()` | `tlv_runtime.c` | Parse blocks `0x01` (field map), `0x02` (group map, optional), and `0x04` (CRC table) into `TlvRuntime` |
| `tlv_runtime_init()` | `tlv_runtime.c` | Zero-initialise a `TlvRuntime` before use |
| `tlv_crc_validate()` | `tlv_crc_validate.c` | Re-compute CSV CRC-32s and compare against TLV-embedded values |
| `tlv_variant_build_array()` | `tlv_variant.c` | Walk data region; populate `WhdVariantArray` |
| `tlv_group_build()` | `tlv_group.c` | Sort variant array; produce `WhdGroupSet` with group boundaries |
| `profile_binder_load()` | `profile_binder.c` | Parse `.profile` INI; resolve tokens to numeric IDs |
| `token_hash8()` | `profile_binder.c` (internal) | FNV-1a 8-bit hash fallback for tokens absent from CSV |
| `whd_build_selection_plan()` | `selection_plan.c` | Build Cartesian lane plan from slash-bucket fields |
| `tlv_select_run()` | `tlv_select.c` | Score variants; pick winner per lane per group |
| `tlv_results_write_file()` | `tlv_results.c` | Write selected filenames to output file |
| `tlv_results_collect_list()` | `tlv_results.c` | Collect selected filenames into `WhdTlvStringList` |
| `whd_search_build_group_allow_list()` | `whd_search.c` | Build group allow list from search pattern; uses block `0x02` canonical name or `base_name` fallback |
| `whd_filter_run()` | `tlv_filter.c` | Internal pipeline orchestrator — stages 1–6 |
| `whdtlv_filter_to_list()` | `whdtlv_filter_facade.c` | Public API: run pipeline; return string list |
| `whdtlv_filter_to_file()` | `whdtlv_filter_facade.c` | Public API: run pipeline; write results to file |

---

## Further Reading

- [docs/tlv-filtering-overview.md](../../tlv-filtering-overview.md) — high-level filtering overview
- [docs/profile_system.md](../../profile_system.md) — full profile file format reference
- [docs/whdtlv_public_filter_facade.md](../../whdtlv_public_filter_facade.md) — public facade usage examples and error codes
- [01-architecture-and-creation-pipeline.md](01-architecture-and-creation-pipeline.md) — how the TLV is created
- [03-extensibility-guide.md](03-extensibility-guide.md) — how to add fields, tokens, and profiles without recompiling
