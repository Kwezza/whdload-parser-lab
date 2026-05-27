# TLV Endianness Divergence Analysis

> **SUPERSEDED — 2026-05-27.**  All items (1–12) are complete.  The TLV format
> is now uniformly big-endian.  This file is retained as a historical record of
> the divergence and the remediation plan.  For the current byte-order
> specification see [tlv-filtering-overview.md](tlv-filtering-overview.md).

**Date:** 2026-05-26  
**Scope:** `src/whdtlv/core/tlv_builder.c`, `src/whdtlv/filtering/`, `tools_src/dat_to_tlv_main.c`

---

## Original Design Intent

The TLV format was designed to be **entirely big-endian** (Motorola byte order), native to
the 68000 CPU.  The idea was straightforward: the PC builder, being faster, would perform
any byte-swapping at build time so the Amiga runtime could load the binary and read all
integer fields directly without any decoding overhead.

---

## What Has Diverged

The format is now **mixed-endian**.  Most structural framing fields were committed early
when `tlv_builder.c` used raw `fwrite()` on x86, silently producing little-endian output.
Only `group_id` and `archive_info` were added later with explicit big-endian encoding.

### Currently little-endian on disk (diverged from original design)

| Field | Width | Location written | Source line(s) |
|---|---|---|---|
| Block `0x01` `map_size` | uint16 | `tlv_write_metadata_map()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&map_size, 2, 1, file)` |
| Block `0x02` `payload_size` | uint16 | `tlv_write_group_map()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&payload_size, 2u, 1u, file)` |
| Block `0x02` `group_count` | uint16 | `tlv_write_group_map()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&group_count, 2u, 1u, file)` |
| Block `0x04` `payload_size` | uint16 | `tlv_write_csv_fingerprints()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&payload_size, 2, 1, file)` |
| Block `0x04` `count` | uint16 | `tlv_write_csv_fingerprints()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&count, 2, 1, file)` |
| Block `0x04` CRC-32 values | uint32 per entry | `tlv_write_csv_fingerprints()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&crc, 4, 1, file)` |
| Data record `value_length` | uint16 per field entry | `tlv_write_record_to_file()` | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `fwrite(&entry->length, 2, 1, file)` |
| CSV-backed token IDs | uint32 per data field value | every CSV field in data records | stored as host-native LE uint32 |

The `fwrite()` calls above pass a pointer to a native C integer.  On x86/x64 that integer is
always little-endian.  There is no explicit byte-swap before the write.

The reader side compensates with explicit LE helpers in [tlv_runtime.c](../src/whdtlv/filtering/tlv_runtime.c)
(`u16_le`, `u32_le`), [tlv_variant.c](../src/whdtlv/filtering/tlv_variant.c) (`u16_le` for
`value_length`), and [tlv_select.c](../src/whdtlv/filtering/tlv_select.c) (`read_u32_le` for
token IDs).  Those helpers exist **because** the writer is LE, not because the format was
designed that way.

[tlv_reader.c](../src/whdtlv/filtering/tlv_reader.c) contains a comment acknowledging this
explicitly:

> *"The plan's 'big-endian' comment describes the aspirational Amiga-native format; the current
> files produced by tlv_builder.c are little-endian."*

There is a secondary divergence in `tlv_builder.c`'s own internal read path
`tlv_read_csv_fingerprints()` — it uses raw `fread()` for the 4-byte CRC values, which
works today only because both the write and read sides run on x86.

### Currently big-endian on disk (already matches Amiga native)

| Field | Width | Written by | How |
|---|---|---|---|
| `group_id` value (per data record and in block `0x02` entries) | uint16 | [tlv_builder.c](../src/whdtlv/core/tlv_builder.c) `tlv_session_inject_group_ids()` | Explicit `id_be[0] = id >> 8; id_be[1] = id & 0xFF` |
| `archive_size_kib` (inside `archive_info` payload) | uint32 | [dat_to_tlv_main.c](../tools_src/dat_to_tlv_main.c) `encode_archive_info()` | `encode_u32_be()` helper |
| `archive_crc32` (inside `archive_info` payload) | uint32 | [dat_to_tlv_main.c](../tools_src/dat_to_tlv_main.c) `encode_archive_info()` | `encode_u32_be()` helper |

`group_id` and `archive_info` were added after the initial builder was working, so they
were written with explicit big-endian helpers from the start.

---

## Conversion Plan

This is a deliberate format reset.  The software has not been released, so there is no
compatibility obligation with any existing TLV file.  All `.tlv` files in `output/` are
build artefacts and will be regenerated as part of the plan.  The goal is to establish a
clean, all-big-endian on-disk format as the permanent baseline before any public release.

The steps are ordered so that no intermediate state leaves a code path knowingly broken.
The completion gate for every item is: **clean `make` build** + **inline comments in
changed files updated to reflect the new byte order**.  Test runs and documentation
updates are called out explicitly at the points where they apply.

---

### Item 1 — Source-tree audit  ✓ COMPLETE

Before making any changes, grep the full source tree for every site that reads or writes
a multi-byte TLV value without an explicit byte-order operation.  The audit output becomes
the definitive checklist that items 3–10 must fully discharge.

Search patterns to run against `src/`, and `tools_src/`:

```
fwrite(&       -- raw fwrite of a local variable (potential implicit-LE write)
fread(&        -- raw fread into a local variable (potential implicit-LE read)
u16_le         -- known LE decode helper (all instances must be replaced)
u32_le         -- known LE decode helper (all instances must be replaced)
read_u32_le    -- known LE decode helper (all instances must be replaced)
```

Record every hit with its file, function, and field name.  Any site not addressed by
items 3–10 below must be resolved before the plan is considered complete.

**Gate: audit checklist produced; `make` clean (no code changes yet).**

> **COMPLETE — 2026-05-27.**  All five patterns were run against `src/` and `tools_src/`.
> 30 sites recorded in [endianness-audit.md](endianness-audit.md) with resolving-item
> assignments.  Three `fread` sites in `tlv_read_metadata_map` and
> `tlv_read_record_with_metadata` (L830, L899, L920) are folded into Item 4 scope.
> Two formerly unassigned gaps (`language_bitfield` uint16 L1172 and `sps_id` uint32 L1209
> in `filename_processor.c`) were inspected and confirmed as live TLV multi-byte value
> payload writes; both are folded into Item 9 as sub-group B (non-CSV numeric payloads).
> No current functional read path was found for either field (see Item 9 and the audit
> file for details).  Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.
---

### Item 2 — Add write helpers to `tlv_builder.c` ✓ COMPLETE

**Completed:** 2026-05-27.  `write_u16_be` and `write_u32_be` added as static helpers in
the `TLV File I/O` section of [tlv_builder.c](../src/whdtlv/core/tlv_builder.c), immediately
before `tlv_write_metadata_map`.  Style matches `encode_u32_be` in `dat_to_tlv_main.c`.
Build gate passed (zero errors; unused-function warnings expected until Item 3 consumes both helpers).

Add two static helpers following the same style as `encode_u32_be` already in
[dat_to_tlv_main.c](../tools_src/dat_to_tlv_main.c).  No behaviour changes at this
point — these are infrastructure only.

```c
static void write_u16_be(FILE *f, uint16_t v)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v & 0xFF);
    fwrite(buf, 1, 2, f);
}

static void write_u32_be(FILE *f, uint32_t v)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >>  8);
    buf[3] = (uint8_t)(v & 0xFF);
    fwrite(buf, 1, 4, f);
}
```

**Gate: `make` clean; comments in `tlv_builder.c` updated to note BE write helpers available. — PASSED 2026-05-27**

---

### Item 3 — Replace raw `fwrite` calls for block framing fields in `tlv_builder.c` ✓ COMPLETE

Every `fwrite` that passes a `uint16_t *` or `uint32_t *` for a block header or
`value_length` field (other than the `group_id` `id_be` write, which is already
correct) must be replaced with the helpers from item 1.  Do **not** touch the CSV
token ID write path yet — that comes in item 7.

| Function | Field | Old call | Replacement |
|---|---|---|---|
| `tlv_write_metadata_map` | `map_size` | `fwrite(&map_size, 2, 1, file)` | `write_u16_be(file, map_size)` |
| `tlv_write_csv_fingerprints` | `payload_size` | `fwrite(&payload_size, 2, 1, file)` | `write_u16_be(file, payload_size)` |
| `tlv_write_csv_fingerprints` | `count` | `fwrite(&count, 2, 1, file)` | `write_u16_be(file, count)` |
| `tlv_write_csv_fingerprints` | each `crc` | `fwrite(&crc, 4, 1, file)` | `write_u32_be(file, crc)` |
| `tlv_write_group_map` | `payload_size` | `fwrite(&payload_size, 2u, 1u, file)` | `write_u16_be(file, payload_size)` |
| `tlv_write_group_map` | `group_count` | `fwrite(&group_count, 2u, 1u, file)` | `write_u16_be(file, group_count)` |
| `tlv_write_record_to_file` | `entry->length` (value_length) | `fwrite(&entry->length, 2, 1, file)` | `write_u16_be(file, entry->length)` |

**Gate: `make` clean; inline format comments in changed functions updated to read "big-endian" at every affected field.**

> **COMPLETE — 2026-05-27.**  All seven `fwrite` sites replaced with `write_u16_be` /
> `write_u32_be` calls.  Inline wire-format comments in `tlv_write_csv_fingerprints`,
> `tlv_write_metadata_map`, `tlv_write_group_map`, and `tlv_write_record_with_metadata`
> updated to read "big-endian".  Build gate: `make build/host/dat_to_tlv.exe` — clean,
> zero warnings.

---

### Item 4 — Fix `tlv_read_csv_fingerprints()` in `tlv_builder.c` ✓ COMPLETE

The fingerprint writer was updated in item 3.  The builder's own internal re-read path
`tlv_read_csv_fingerprints()` must be fixed immediately — not deferred — so the builder
is never left with a knowingly broken internal round-trip.  It currently uses raw
`fread()` which only worked because the old writer was also x86-native.

Replace with explicit BE reads:

- `fread(&payload_size, 2, 1, file)` → read 2 bytes into `uint8_t buf[2]`, decode as
  `(uint16_t)((buf[0] << 8) | buf[1])`
- `fread(&count, 2, 1, file)` → same pattern
- `fread(&out_map->entries[i].crc32, 4, 1, file)` → read 4 bytes, decode as BE uint32

**Gate: `make` clean; the stale LE assumption comment at the top of
`tlv_read_csv_fingerprints()` removed or updated.**

> **COMPLETE — 2026-05-27.**  All six `fread` sites (rows 8–13 in the audit) replaced
> with explicit big-endian reads using `uint8_t buf[]` decode patterns.  Scope extended
> to include the three folded-in sites from the audit: `tlv_read_metadata_map` (row 11,
> `map_size`) and `tlv_read_record_with_metadata` (row 12 skip-path `map_size`, row 13
> `value_length`).  The `tlv_read_csv_fingerprints` docstring updated to note
> big-endian decoding.  `payload_size` is decoded but not used downstream (field is
> advanced past only); suppressed with `(void)payload_size`.  Build gate:
> `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 5 — Rebuild TLV files ✓ COMPLETE

Run `make run` for each pack type to produce fresh TLV files with BE block framing.
Token ID field values are still LE at this point — this is intentional and consistent:
the reader has not been updated yet.

**Gate: `make` clean, output TLVs produced.**

> **COMPLETE — 2026-05-27.**  Two pre-existing issues were resolved before the run could
> succeed: (1) a stray space in `assets_raw/prefs/pack_types.ini` line 6
> (`modifier, archive_form` → `modifier,archive_form`) that caused field-list parsing to
> fail; (2) `MAX_FIELD_COUNT` in `src/whdtlv/io/pack_types_loader.c` raised from 16 to
> 32 — the Games pack type has 21 fields, which exceeded the old limit.  After those
> fixes, `make run` completed cleanly: all five pack types processed with 0 errors
> (DemB 12 entries, Demo 904, GamB 128, Game 3973, Mags 104).  All five `.tlv` files in
> `output/` regenerated.  Build gate: `make build/host/dat_to_tlv.exe` — clean, zero
> warnings.

---

### Item 6 — Update `tlv_runtime.c` block framing reads ✓ COMPLETE

Replace the `u16_le()` calls used to parse block header sizes/counts and the
`u32_le()` call used to read the CRC-32 fingerprint values with BE equivalents.
The existing `tlv_read_u16_be()` and `tlv_read_u32_be()` functions in
[tlv_reader.c](../src/whdtlv/filtering/tlv_reader.c) are already implemented for
this purpose.

| Site in `tlv_runtime.c` | Old helper | Replacement |
|---|---|---|
| `map_size` read in block `0x01` parse | `u16_le(buf + *pos)` | `tlv_read_u16_be(buf + *pos)` |
| `payload_size` / `count` reads in block `0x04` parse | `u16_le(buf + *pos)` | `tlv_read_u16_be(buf + *pos)` |
| CRC-32 read in block `0x04` parse | `u32_le(buf + *pos)` | `tlv_read_u32_be(buf + *pos)` |
| `payload_size` / `count` reads in block `0x02` parse | `u16_le(buf + *pos)` | `tlv_read_u16_be(buf + *pos)` |

**Gate: `make` clean; `u16_le` and `u32_le` static helpers in `tlv_runtime.c` removed
(they are now unused); inline comments updated to state "big-endian".**

> **COMPLETE — 2026-05-27.**  All six `u16_le` / `u32_le` call sites (audit rows 14–19)
> replaced with `tlv_read_u16_be` / `tlv_read_u32_be`.  The local `u16_le`, `u32_le`,
> and `u16_be` static helpers removed from `tlv_runtime.c` (the last was used only for
> `group_id` in `parse_group_map` and is now handled by `tlv_read_u16_be`).  File-header
> comment and all four wire-format block comments updated to read "big-endian".
> Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 7 — Update `tlv_variant.c` value_length read ✓ COMPLETE

Replace the `u16_le()` call that reads each data record's `value_length` with a BE
equivalent.  This is a single call site:

```c
/* Old */
length = u16_le(buffer + pos + 1u);
/* New */
length = tlv_read_u16_be(buffer + pos + 1u);
```

**Gate: `make` clean; `u16_le` static helper in `tlv_variant.c` removed (now unused);
wire format comment at top of file updated to read "big-endian".**

> **COMPLETE — 2026-05-27.**  Single call site (audit row 20, L120) replaced with
> `tlv_read_u16_be(buffer + pos + 1u)`.  `#include "whdtlv/filtering/tlv_reader.h"`
> added to `tlv_variant.c` (not previously included).  The `u16_le` static helper
> (L57) removed.  Wire-format block comment at the top of the file updated: "little-endian"
> → "big-endian" for `value_length`; CSV field values comment updated to "BE uint32".
> Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 8 — Run filter tests *(first test gate)*

At this point block framing and `value_length` are written and read as BE.  CSV token
ID field values are still LE on both sides (writer not changed yet, reader not changed
yet), so filter results should be correct.

```
make test-filter
```

**Gate: `make` clean + all filter tests pass.**

---

### Item 9 — Fix all multi-byte value payload writes in `filename_processor.c` ✓ COMPLETE

All multi-byte value fields written via `tlv_record_add_entry` in `filename_processor.c`
are currently passed as native-endian memory pointers.  On x86 this produces
little-endian on-disk values.  Replace every affected site with explicit big-endian
encoding before the call.

The software has not been released, so there is no compatibility obligation with any
previously generated TLV file.  Existing `.tlv` files in `output/` are build artefacts
and will be regenerated.

#### Sub-group A — CSV-backed uint32 token IDs

These four sites pack a token ID returned from a CSV lookup into the value buffer as a
native-endian `uint32_t` cast.  Replace each with an explicit BE encode:

```c
/* Old — host-native (LE on x86) */
uint32_t id = csv_cache_lookup_loaded(...);
tlv_record_add_entry(record, field_id, (uint8_t *)&id, 4);

/* New — explicit BE */
uint32_t id = csv_cache_lookup_loaded(...);
uint8_t  id_be[4];
id_be[0] = (uint8_t)(id >> 24);
id_be[1] = (uint8_t)(id >> 16);
id_be[2] = (uint8_t)(id >>  8);
id_be[3] = (uint8_t)(id & 0xFF);
tlv_record_add_entry(record, field_id, id_be, 4);
```

Sites (confirmed by audit 2026-05-27):

| Line | Context | Field | Width |
|------|---------|-------|-------|
| 624  | contributor lookup     | `contributor_id` (CSV token) | uint32 |
| 827  | prescan CSV field loop | `id`             (CSV token) | uint32 |
| 1302 | CSV multi-part token   | `token_id`       (CSV token) | uint32 |
| 1335 | CSV single token       | `token_id`       (CSV token) | uint32 |

#### Sub-group B — Non-CSV numeric payloads

Two additional sites (formerly G1/G2 unassigned gaps) write non-CSV multi-byte values
as native-endian pointer casts.  Code inspection on 2026-05-27 confirmed both are live
TLV value payload writes; neither is a CSV token ID.

| Line | Context | Field | Width | Encoding required |
|------|---------|-------|-------|-------------------|
| 1172 | language token loop | `language_bitfield` (language flags) | uint16 | explicit 2-byte BE |
| 1209 | SPS loop            | `sps_id` (raw `atoi` value)          | uint32 | explicit 4-byte BE |

For `language_bitfield` (uint16 BE):

```c
/* Old */
tlv_record_add_entry(record, language_field_id,
                     (const uint8_t*)&language_bitfield, sizeof(language_bitfield));

/* New */
uint8_t lang_be[2];
lang_be[0] = (uint8_t)(language_bitfield >> 8);
lang_be[1] = (uint8_t)(language_bitfield & 0xFF);
tlv_record_add_entry(record, language_field_id, lang_be, 2);
```

For `sps_id` (uint32 BE): apply the same 4-byte BE pattern shown in sub-group A.

#### Read-path status for sub-group B fields (recorded 2026-05-27)

- `language_bitfield`: `variant_index_build` tracks the "language" field but only
  extracts a `csv_id` for entries with `length == 4`; the 2-byte bitfield always
  produces `csv_id = 0` and falls back to a hash comparison that does not match profile
  token IDs.  `tlv_select.c` and `whdtlv_report_profile.c` both skip entries with
  `length < 4`.  **No current functional read path found.**  This must be re-confirmed
  during implementation; if a read path is added before this item is implemented, the
  new read site must also use explicit BE decoding.

- `sps_id`: the "sps" field is not in `variant_index_build`'s tracked list and no
  profile currently binds it as a scored or reported column.  **No current read path
  found.**  Same re-confirmation requirement applies.

**Gate: `make` clean; comments at all changed sites updated to state "stored as big-endian"; sub-group B sites annotated with the field's bit-width and that no read path currently exists.**

> **COMPLETE — 2026-05-27.**  All six audit rows (25–30) resolved.  Sub-group A: four
> CSV-backed `uint32_t` token ID sites (`contributor_id` L624, prescan `id` L827, ampersand
> multi-part `token_id` L1302, single-token `token_id` L1335) each replaced with a local
> `uint8_t [4]` BE buffer pattern before the `tlv_record_add_entry` call; inline comments
> read "stored as big-endian uint32".  Sub-group B: `language_bitfield` (L1172, uint16)
> encoded into a local `lang_be[2]`; `sps_id` (L1209, uint32) encoded into a local
> `sps_be[4]`; both annotated "no current read path; stored as big-endian".  Read-path
> re-confirmation: no new read sites for either sub-group B field found in
> `tlv_variant.c`, `tlv_select.c`, or `whdtlv_report_profile.c` — annotation holds.
> Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 10 — Update token ID read sites in the filtering and reporting layers ✓ COMPLETE

Replace all LE token ID decode helpers with BE equivalents.  These are independent
of the block framing reads updated in items 6 and 7.

| File | Helper to replace | Replacement |
|---|---|---|
| [tlv_select.c](../src/whdtlv/filtering/tlv_select.c) | `read_u32_le(p)` (3 call sites) | `tlv_read_u32_be(p)` |
| [whdtlv_report_profile.c](../src/whdtlv/reporting/whdtlv_report_profile.c) | `read_u32_le_local(p)` | `tlv_read_u32_be(p)` |

**Gate: `make` clean; `read_u32_le` / `read_u32_le_local` static helpers removed (now
unused); stale LE encoding note at the top of `tlv_select.c` removed.**

> **COMPLETE — 2026-05-27.**  All four audit rows (21–24) resolved.  In
> `tlv_select.c`: `#include "whdtlv/filtering/tlv_reader.h"` added; the `read_u32_le`
> static helper (L51) removed; all three call sites (L146 `variant_field_in_bucket`,
> L285 inner scorer loop, L448 `tlv_select_score_variant`) replaced with
> `tlv_read_u32_be`; the file-level wire-format comment updated from "4-byte LE uint32"
> to "4-byte BE uint32".  In `whdtlv_report_profile.c`: `tlv_reader.h` was already
> included; the `read_u32_le_local` static helper (L144) removed and its block comment
> updated to note big-endian decoding via `tlv_read_u32_be`; the single call site
> (L354 `collect_explicit_tokens`) replaced with `tlv_read_u32_be`.
> Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 11 — Rebuild TLV files ✓ COMPLETE

Run `make run` again to produce TLV files with both BE block framing and BE token IDs.
The files produced in item 5 are now stale and must be replaced before tests are run.

**Gate: `make` clean, output TLVs produced.**

> **COMPLETE — 2026-05-27.**  `make run` completed cleanly after a rebuild of
> `tlv_select.o` (the only object that changed since item 5).  All five pack types
> processed with 0 errors: DemB 12 entries, Demo 904, GamB 128, Game 3973, Mags 104.
> All five `.tlv` files in `output/` regenerated with uniform big-endian encoding
> (both block framing and token ID field values).  Build gate:
> `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

### Item 12 — Run all tests and update documentation *(final gate)* ✓ COMPLETE

```
make test-filter
make test-report
```

Once tests pass, update the project documentation to reflect the now-uniform byte order:

- [tlv-filtering-overview.md](../tlv-filtering-overview.md) — replace the mixed-endian
  reference table with an all-BE table; remove the paragraph explaining why framing
  fields were committed as LE.
- [how-it-works/DeepDive/02-filtering-system.md](how-it-works/DeepDive/02-filtering-system.md)
  — update the endian quick-reference table; remove the "mixed-endian convention"
  language.
- [tlv_reader.c](../src/whdtlv/filtering/tlv_reader.c) — remove or rewrite the comment
  that reads *"The plan's 'big-endian' comment describes the aspirational Amiga-native
  format; the current files produced by tlv_builder.c are little-endian."*
- This file ([endianness-divergence.md](endianness-divergence.md)) — mark as superseded
  or archive it once the format is uniform.

**Gate: `make` clean + all tests pass + documentation updated.**

> **COMPLETE — 2026-05-27.**  Both test suites passed after the fixture TLVs in
> `assets_raw/TLV/` were replaced with the freshly built BE-encoded versions from
> `output/` (DemB, GamB, Game, Mags).  Filter suite: 38 passed, 0 failed.
> Report suite: 56 passed, 0 failed (all 13 previously failing assertions fixed by
> the fresh BE fixtures).  Documentation updated:
> (1) `tlv_reader.c` stale LE comment (lines 6–12) rewritten to state uniform
> big-endian encoding and name the BE read helpers;
> (2) `tlv-filtering-overview.md` endian reference table converted to all-BE, LE
> rationale paragraph removed, "Mixed-endian convention is intentional and stable"
> note replaced with an all-BE note, and the inline token-ID reference at line 281
> updated from LE to BE;
> (3) `02-filtering-system.md` mixed-endian paragraph replaced with a uniform-BE
> paragraph, endian quick-reference table updated to all-BE with correct helper
> names, and the inline `read_u32_le()` reference updated to `tlv_read_u32_be()`;
> (4) this file marked superseded with a banner at the top pointing to
> `tlv-filtering-overview.md` as the authoritative byte-order reference.
> Build gate: `make build/host/dat_to_tlv.exe` — clean, zero warnings.

---

## Risks

### High — silent data corruption in token IDs

CSV-backed token IDs are uint32 values.  If the writer is changed to encode them BE but
one read site still uses a LE decode (or vice versa), filtering will produce wrong results
with no error — variants will score against the wrong token IDs and winners will be silently
incorrect.  This is the highest-risk part of the change because there are multiple
independent read sites (`tlv_select.c`, `whdtlv_report_profile.c`) and any missed one
produces silent wrong output.

### High — existing TLV files become unreadable immediately

The moment the writer is updated, any TLV file produced before the change is broken for
the updated reader.  Tests that load fixture TLV files (e.g. `tests/filtering/` and
`tests/reporting/`) will fail or — if not checking field values carefully — silently pass
against misinterpreted data.  All fixture TLVs must be regenerated before running tests.

### Medium — the `tlv_read_csv_fingerprints` path in the builder

This internal builder-side reader is only exercised when the builder re-reads a TLV it
has written (for metadata inspection or incremental update).  If its endianness is not
updated in step 4, the builder will silently load wrong CRC values when re-reading an
existing TLV, which can lead to spurious CRC mismatch warnings or false positive
validations.

### Medium — `whdtlv_report_profile.c` `read_u32_le_local`

The reporting tool has its own inline LE read helper that is independent of the filtering
layer.  It is easy to overlook when updating the other sites.

### Low — `group_id` and `archive_info` (already BE, no change needed)

These fields are already written and read as BE.  Do not touch the `group_id` inject path
(`tlv_session_inject_group_ids`) or the `encode_u32_be` / `encode_archive_info` helpers
in `dat_to_tlv_main.c`.

### Low — vbcc / Amiga build compatibility

All proposed write helpers use only byte shifts and masks on `uint8_t` arrays — C89-safe
and portable to vbcc.  No `htonl`/`htons` (not available on AmigaOS), no `bswap`
intrinsics, and no `<arpa/inet.h>` dependency.  The pattern matches what the codebase
already uses for `group_id` and `archive_info`.

### Note — Amiga runtime crash is a pre-existing issue

The Amiga binary already crashes at runtime (see AGENTS.md and
[docs/HANDOVER_2026-05-01.md](HANDOVER_2026-05-01.md)).  Completing the endianness
conversion does not fix or worsen that crash — it is a separate issue.  The Amiga
target cannot be used as a validation environment until the crash is resolved.

---

## Existing Tests That Cover the Affected Code Paths

### `tests/filtering/test_filter_facade.c`

The primary integration test.  Runs the full filter pipeline (`tlv_reader` → `tlv_runtime`
→ `tlv_variant` → `tlv_select`) against real TLV files in `output/`.  Any missed
byte-swap in the header parsing or token ID scoring will cause wrong filter results or
an outright load failure.

Run with:
```
make test-filter
```

### `tests/reporting/test_report_csv.c`

Covers the reporting layer including Test 12 which specifically exercises the
`archive_info` CRC-32 rendering.  Because `archive_info` is already BE, this test
also acts as a canary that the `archive_info` path is not accidentally broken during
conversion.  Also checks that `group_id` appears correctly in wide and long output.

Run with:
```
make test-report
```

### `tests/reporting/test_profile_report.c`

Test 5 performs a filter-parity check: the profile report's winner set must match
the output of `whdtlv_filter_to_list()`.  Any token ID endianness mismatch between
the writer and the reader will break parity here.

Run with:
```
make test-report
```

### `tests/reporting/test_effective_columns.c`, `test_language_tokens.c`, `test_csv_alias.c`

These exercise CSV lookup and token matching.  They are less directly tied to on-disk
endianness but they will catch regressions in the CSV-to-token-ID pipeline if the
value encoding is wrong.
