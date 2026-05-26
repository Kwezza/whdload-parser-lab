# TLV Endianness Divergence Analysis

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

### Item 1 — Source-tree audit

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

---

### Item 2 — Add write helpers to `tlv_builder.c`

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

**Gate: `make` clean; comments in `tlv_builder.c` updated to note BE write helpers available.**

---

### Item 3 — Replace raw `fwrite` calls for block framing fields in `tlv_builder.c`

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

---

### Item 4 — Fix `tlv_read_csv_fingerprints()` in `tlv_builder.c`

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

---

### Item 5 — Rebuild TLV files

Run `make run` for each pack type to produce fresh TLV files with BE block framing.
Token ID field values are still LE at this point — this is intentional and consistent:
the reader has not been updated yet.

**Gate: `make` clean, output TLVs produced.**

---

### Item 6 — Update `tlv_runtime.c` block framing reads

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

---

### Item 7 — Update `tlv_variant.c` value_length read

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

### Item 9 — Fix the CSV token ID write path in `filename_processor.c`

CSV-backed token IDs are currently packed into the value buffer as a native-endian
`uint32_t` cast.  Replace each site where a 4-byte token ID is assembled with an
explicit BE encode:

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

Check all sites in `filename_processor.c` where a 4-byte ID is passed to
`tlv_record_add_entry`.

**Gate: `make` clean; comments at changed sites updated to state "stored as big-endian uint32".**

---

### Item 10 — Update token ID read sites in the filtering and reporting layers

Replace all LE token ID decode helpers with BE equivalents.  These are independent
of the block framing reads updated in items 6 and 7.

| File | Helper to replace | Replacement |
|---|---|---|
| [tlv_select.c](../src/whdtlv/filtering/tlv_select.c) | `read_u32_le(p)` (3 call sites) | `tlv_read_u32_be(p)` |
| [whdtlv_report_profile.c](../src/whdtlv/reporting/whdtlv_report_profile.c) | `read_u32_le_local(p)` | BE equivalent |

**Gate: `make` clean; `read_u32_le` / `read_u32_le_local` static helpers removed (now
unused); stale LE encoding note at the top of `tlv_select.c` removed.**

---

### Item 11 — Rebuild TLV files

Run `make run` again to produce TLV files with both BE block framing and BE token IDs.
The files produced in item 5 are now stale and must be replaced before tests are run.

**Gate: `make` clean, output TLVs produced.**

---

### Item 12 — Run all tests and update documentation *(final gate)*

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
