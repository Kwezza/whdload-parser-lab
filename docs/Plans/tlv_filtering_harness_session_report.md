# TLV Filtering Harness — Session Report

**Date:** 2026-05-08 (updated with group_id patch)  
**Plan reference:** `docs/Plans/tlv_filtering_harness_implementation_plan.md`  
**Branch:** `main`

---

## 1. Session Overview

This session implemented the complete TLV runtime filtering harness described in the plan,
working through all ten Stages (A through J) split across seven implementation chunks, followed
by two post-session patches (split rejection counters; group_id / group-map support).  Each
chunk was built, compiled without warnings, and validated against the running TLV before
moving to the next.

The harness is hosted at `tools/filter_harness/` and the reusable filtering subsystem lives
entirely inside `src_raw/filtering/` with public headers in `include_raw/filtering/`.

---

## 2. Planned vs Delivered — Stage by Stage

### Stage A — Compile-Only Skeleton ✅

**Plan requirement:** folder structure, headers, empty stubs, harness entry point; `filter_harness --help` works.

**Delivered:** All 8 headers (`tlv_filter.h`, `tlv_reader.h`, `tlv_runtime.h`,
`tlv_crc_validate.h`, `tlv_variant.h`, `tlv_group.h`, `tlv_select.h`, `tlv_results.h`) and
corresponding stub `.c` files created.  Makefile target `filter_harness` wired.  `--help`
prints full option list.

---

### Stage B — TLV File Load and Header Validation ✅

**Plan requirement:** `tlv_reader_load()`, `tlv_reader_free()`, `tlv_reader_validate_header()`; `--dump-header` works.

**Delivered:** `tlv_reader.c` fully implemented.  Magic byte, version, and endian marker
validated.  `tlv_runtime.c` parses the field-map block (`0x01`), the optional group-map
block (`0x02`), and the CRC block (`0x04`) in that order, and exposes `data_offset` and
`group_id_field_id`.  `--dump-header` confirmed against `output/Game(2026-04-17).tlv`.

---

### Stage C — Field Map and CRC Block Dump ✅

**Plan requirement:** parse TLV field map and embedded CSV CRC block; `--dump-fields`, `--dump-crcs`.

**Delivered:** `TlvFieldMap` (up to 252 entries, each `{id, name[32]}`), `TlvCrcMap`, and
`TlvGroupMap` parsed at load time.  `tlv_runtime_field_id()` / `tlv_runtime_field_name()`
provide field-name↔ID lookup.  `tlv_runtime_group_name()` looks up a canonical group name
from the group map.  `--dump-fields` and `--dump-crcs` operational.

Sample output from the rebuilt Games TLV (now 261 KB, 21 fields, group_id at 0x05):

```
--- TLV Header ---
  Format version : 1
  Field map      : yes
  CRC block      : yes
  Group map      : no
  Grouping mode  : group_id field (0x05)
  Data offset    : 422 bytes
  File size      : 261573 bytes
--- Field Map (21 fields) ---
  id=0x04  name=display_name
  id=0x05  name=group_id
  id=0x06  name=sps
  id=0x07  name=publisher
  id=0x08  name=version
  id=0x09  name=chipset
  id=0x0A  name=contributors
  id=0x0B  name=crack_groups
  id=0x0C  name=disks
  id=0x0D  name=language
  id=0x0E  name=media
  id=0x0F  name=memory
  id=0x10  name=software_houses
  id=0x11  name=video
  id=0x12  name=cover_disks
  id=0x13  name=compilations
  id=0x14  name=variant_tags
  id=0x15  name=archive_info
  id=0x16  name=scene_group
  id=0x17  name=issue
  id=0x18  name=magazines
```

Pack-type fields now begin at 0x06 (previously 0x05).  `group_id` occupies 0x05 as a
reserved implicit field.  The group map block (0x02) is parsed between the field map and
the CRC block when present; its absence is not fatal.

---

### Stage D — CRC Validation Against assets_raw/defs ✅

**Plan requirement:** validate 13 embedded CSV CRCs against live files; strict and warn modes.

**Delivered:** `tlv_crc_validate.c` uses `fopen("r")` + `fgets` (text mode) to match the
same mode used by the builder, producing identical CRC-32 values on Windows even with
`\r\n` line endings.  All 13 CSVs validate clean.

```
CSV CRC: OK  (13 files checked)
```

Strict mode aborts if any CSV is missing, unreadable, or mismatched.  `--warn-crc` allows
continuation with a warning line.

---

### Stage E — Profile Load and Bind ✅

**Plan requirement:** load `.profile` file, bind fields to TLV field IDs, resolve token IDs.

**Delivered:** New self-contained `profile_binder.c` — no dependency on old pipeline modules
(`FieldRegistry`, `GlobalCSVManager`).  Reads INI sections, resolves tokens against CSV
files in `defs_dir`, falls back to FNV-1a 8-bit hash (same as the builder) when a token is
not found so both sides agree.  Populates `rank_by_id[256]` for O(1) rank lookup during
scoring.  `--dump-profile` shows bound fields, weights, and resolved token IDs.

---

### Stage F — Variant View Scan ✅

**Plan requirement:** parse TLV data records into `WhdVariantView` entries.

**Delivered:** `tlv_variant_build()` in `tlv_variant.c` scans `buffer[data_offset..]` entry
by entry (`[field_id:1][length:2LE][value:N]`).  Each `display_name` entry creates a new
view.  Filename and `base_name` are heap-allocated NUL-terminated copies.

When `group_id_field_id` is non-zero (resolved from the TLV field map), the scanner reads
the 2-byte big-endian `group_id` payload into `WhdVariantView.group_id` and sets
`has_group_id = 1`.  The group_id entry is explicitly excluded from `fields[]` via `else if`
so it does not inflate `interior_fields` or participate in profile scoring.

`original_index` is set to the 0-based scan position as each variant is discovered.  It is
runtime-only (never stored in the TLV) and is used as a deterministic secondary sort key so
tie-breaks within a group preserve first-encountered TLV order.

**base_name derivation:** still computed from the `_v<digit>` heuristic on `display_name`
via `derive_group_name()` (shared `src_raw/group_util.c`).  Used as the fallback group
name when the TLV has no group map.  Derivation is always performed regardless of whether
group_id is present.

Result: **3,973 variant views** from `Game(2026-04-17).tlv` (261.6 KB, rebuilt TLV).

---

### Stage G — Grouping ✅

**Plan requirement:** group variants by logical game/base name; `--dump-groups` works.

**Delivered:** `tlv_group_build()` supports two paths selected by the `has_group_id_field`
argument (derived from `rt.group_id_field_id != 0`).

**group_id path** (new TLVs): sorts `sorted_indices[]` by `(group_id ASC,
original_index ASC)` and detects boundaries on group_id change.  The secondary
`original_index` sort makes `qsort` deterministic within a group so first-encountered
wins on score ties.  `WhdVariantGroup.group_id` is set from the first variant's
`group_id`.  `dump_groups()` calls `tlv_runtime_group_name()` for the canonical
display name from the group map block, falling back to `base_name` if the group map is
absent or the ID is not found.

**Fallback path** (old TLVs without group_id): sorts by `(base_name ASC,
original_index ASC)`.  `WhdGroupSet.fallback_count` records the number of variants
grouped this way.  `--dump-groups` reports `[display_name heuristic]` and prints the
fallback count.

New `--dump-groups` output (group_id path, new TLV, `--limit 5`):

```
--- Variant Groups: 2904 groups, 3973 variants  [group_id field] ---
  [0001] 1000ccTurbo  (1 variant)
      1000ccTurbo_v1.0
  [0002] 1000Miglia  (1 variant)
      1000Miglia_v1.2
  [0003] 1869  (5 variants)
      1869_v1.0_AGA
      1869_v1.0_De_AGA_1653
      1869_v1.2
      1869_v1.2_De_0417
      1869_v1.2_Pl
  [0004] 1943  (1 variant)
      1943_v1.4
  [0005] 1stDivisionManager  (1 variant)
      1stDivisionManager_v1.0_1800
  ... (showing 5 of 2904 groups)
```

Old TLV fallback (`games_no_log_test.tlv`, `--warn-crc --dump-groups --limit 3`):

```
--- Variant Groups: 2833 groups, 3860 variants  [display_name heuristic] ---
--- Fallback group derivations: 3860 ---
  [1] 1000Miglia  (1 variant)
      ...
```

3,973 variants produce 2,904 groups — an average of 1.37 variants per group.

---

### Stage H — Score One Group ✅

**Plan requirement:** per-variant score display for a named group; `--group <name>`.

**Delivered:** `tlv_select_score_variant()` scores a single view against a `WhdBoundProfile`.
Scoring rules from `profile_system.md` are fully implemented: exclude-first rejection,
rank-based field score, default token fallback, interior-fields bonus.
`dump_scored_group()` in the harness displays each variant with score and [selected]/
[rejected] labels.

```
--- Group: 1869  (5 variants) ---
             1869_v1.0_AGA                             score=693
  [selected] 1869_v1.0_De_AGA_1653                     score=695
             1869_v1.2                                 score=392
             1869_v1.2_De_0417                         score=394
             1869_v1.2_Pl                              score=393
```

Interior-fields bonus (1 point) correctly selects `1869_v1.0_De_AGA_1653` over `1869_v1.0_AGA`
because the `_1653` SPS tag is an additional interior field.

```
--- Group: AlienBreed3D  (4 variants) ---
  [selected] AlienBreed3D_v1.3_NoMusic_CD32            score=4
  [rejected] AlienBreed3D_v1.3_AGA_0624
             AlienBreed3D_v1.3_CD32                    score=3
  [rejected] AlienBreed3D_v1.3_NoMusic_AGA_0624
```

(Profile: `chipset_legacy_only.profile` — AGA variants excluded.)

---

### Stage I — Full Selection and Output File ✅

**Plan requirement:** run the selector across all groups, write output file, print summary.

**Delivered:** `whd_filter_run()` in `tlv_filter.c` chains the full pipeline: load →
validate CRCs → bind profile → build variants → group → select → write file.
`tlv_results_write_file()` writes one filename per line.

---

## 3. Test Results

All tests performed on host (Windows, GCC, `-std=c99 -O2 -Wall -Wextra`).
Zero compiler warnings at every stage.  Tests below use the rebuilt TLV
(`Game(2026-04-17).tlv`, 261.6 KB) which includes `group_id` at 0x05.

### 3.1 Profile: pal_aga_4mb (prefer AGA, language EN, fast memory)

```
CSV CRC: OK  (13 files checked)
TLV     : output\Game(2026-04-17).tlv
Profile : assets_raw\profiles\pal_aga_4mb.profile
Variants: 3973
Groups  : 2904
Selected: 2904
Variants rejected: 0
Groups rejected  : 0
Output  : output\filter_results_aga.txt
```

Result: 2,904 lines in output file.  No variants are rejected because all groups have at
least one non-excluded variant (no field in this profile has an absolute exclude that covers
the whole group).

### 3.2 Profile: chipset_legacy_only (ECS/OCS preferred, AGA excluded)

```
CSV CRC: OK  (13 files checked)
TLV     : output\Game(2026-04-17).tlv
Profile : assets_raw\profiles\chipset_legacy_only.profile
Variants: 3973
Groups  : 2904
Selected: 2832
Variants rejected: 234
Groups rejected  : 72
Output  : output\filter_results_legacy.txt
```

Result: 72 groups have all variants excluded.  234 individual variant scorings were
rejected by the AGA/CD32 exclude rules across those and other groups.  `--dump-rejected`
confirms:

```
--- Rejected Groups ---
  ACSYSDemo
  Aladdin
  AlfabetSmierci
  AlienBreed3D2
  AlienBreed3DDemoLatestDemo
  AlienBreed3DDemoPlayableDemo
  AlienBreed3DDemoRollingDemo
  AllNewWorldOfLemmings
  ArcadeSnooker
  BasketIslandDemo1
  ... (limit reached)
```

### 3.3 Profile: chipset_aga_only (AGA preferred, no exclusions)

```
CSV CRC: OK  (13 files checked)
TLV     : output\Game(2026-04-17).tlv
Profile : assets_raw\profiles\chipset_aga_only.profile
Variants: 3973
Groups  : 2904
Selected: 2904
Variants rejected: 0
Groups rejected  : 0
Output  : output\filter_results_aga_only.txt
```

Result: 2,904 lines.  No exclusions set in this profile.

### 3.4 Scoring correctness

| Group | Profile | Expected winner | Actual winner | Pass |
|-------|---------|-----------------|---------------|------|
| 1869 | pal_aga_4mb | AGA variant with most interior fields | `1869_v1.0_De_AGA_1653` (score 695) | ✅ |
| AlienBreed3D | pal_aga_4mb | AGA variant with most interior fields | `AlienBreed3D_v1.3_NoMusic_AGA_0624` (score 695) | ✅ |
| AlienBreed3D | chipset_legacy_only | Non-AGA with most interior fields | `AlienBreed3D_v1.3_NoMusic_CD32` (score 4) | ✅ |

### 3.5 CRC validation correctness

Text-mode CRC (`fopen("r")` + `fgets`) matches the builder exactly on Windows.
Binary-mode (`fopen("rb")` + `fread`) would produce different values due to `\r\n`→`\n`
translation.  Fixed in Stage D and confirmed stable.

---

## 4. Deviations from Plan

| Plan item | Actual | Reason |
|-----------|--------|--------|
| Plan said reuse existing `profile_loader.c` | New `profile_binder.c` written | Old loader depends on `FieldRegistry`, `GlobalCSVManager`, and `filter_profile.c` — a dependency chain that would have dragged in the entire old pipeline. The self-contained binder is smaller, faster to understand, and already compatible with the filtering-subsystem types. |
| Plan said `filename` in `WhdVariantView` points into TLV buffer | `filename` is a heap-allocated NUL-terminated copy | TLV values are not NUL-terminated in the wire format (length-prefixed only). Pointing into the buffer without copying would be unsafe for use as a C string. |
| Plan suggested a canonical grouping key from `dat_to_tlv` | `group_id` field (0x05) now emitted by the builder; runtime uses it when present | The builder was updated to assign sequential `group_id` values (derived from the `_v<digit>` heuristic at build time) and embed them in each variant record.  The filtering runtime resolves the field ID from the embedded field map, reads the 2-byte big-endian payload, and groups by integer comparison.  Old TLVs without `group_id` fall back to the heuristic string path. |
| Plan said reject-count should count rejected variants | Both counts implemented: `rejected_variants_count` (234 = individual variants excluded across all groups) and `rejected_groups_count` (72 = groups where every variant was excluded) | Both numbers are independently useful. The summary now prints both on separate labeled lines. |

---

## 5. Known Limitations

- **qsort is not stable, but original_index provides a deterministic secondary key:**
  the sort comparators use `(primary_key ASC, original_index ASC)` as a two-part key so
  within-group order is always deterministic and preserves TLV scan order.  On the
  group_id path the primary key is a uint16 integer; on the fallback path it is a string.
  The strict `>` in the selector (not `>=`) ensures first-in-sorted-order wins on
  equal scores, which is equivalent to first-encountered in the TLV.

- **base_name heuristic retained for fallback:** `derive_group_name()` is still called for
  every variant so `base_name` is always available as a display fallback when the group map
  is absent.  On new TLVs with `group_id`, grouping uses the integer key and `base_name` is
  only used if the group map lookup returns NULL.

- **Multi-value fields scored as best-of:** when a variant has more than one entry for the
  same field (e.g. two `language` entries), the scorer takes the best rank across all values.
  This is the correct behaviour for multi-valued fields like `language` and `contributors`
  but could over-score if a variant has conflicting chipset entries, which should not occur
  in practice.

- **Amiga binary not yet tested:** the `filter_harness` target currently builds only for
  host.  The Amiga binary path for the filtering subsystem is written to C89 / vbcc
  standards and uses no host-only constructs, but has not been compiled or run on hardware.

---

## 6. Amiga Testing Plan

### 6.1 Prerequisites

| Requirement | Notes |
|-------------|-------|
| WinUAE or real hardware | 68020+ recommended; 68000 will work but is slow |
| vbcc Amiga cross-compiler | Installed at `C:/VBCC` or override `VBCC=` in Makefile |
| Amiga target binary | `make TARGET=amiga` → `build/amiga/filter_harness` |
| Transfer method | ADF, HDF, TCP/IP (`SAS/C amitcp`), USB PCMCIA, or SD card |

### 6.2 Step 1 — Build the Amiga Binary on Host

```bat
make TARGET=amiga filter_harness
```

Expected output: `build/amiga/filter_harness` (AmigaOS executable, 68000 code).

If vbcc is not in `PATH`, set `VBCC=C:\path\to\vbcc` before running make.

> **Note:** The existing `dat_to_tlv` Amiga binary is known to crash at runtime
> (see AGENTS.md).  The `filter_harness` binary has a much simpler execution profile —
> it reads, not writes — so the crash risk should be lower, but stack size is still critical.

### 6.3 Step 2 — Prepare the Amiga Environment

Files to copy to Amiga (e.g. `DH0:FilterTest/`):

```
build/amiga/filter_harness          (the binary)
output/Game(2026-04-17).tlv        (191 KB — fits on any HD)
assets_raw/defs/                   (all CSV definition files)
assets_raw/profiles/pal_aga_4mb.profile
assets_raw/profiles/chipset_aga_only.profile
assets_raw/profiles/chipset_legacy_only.profile
```

### 6.4 Step 3 — Set Stack Size

Always set a large stack before running.  The binary loads a 191 KB TLV into heap memory and
allocates ~3,973 variant views, but stack usage during qsort and the scoring pass is also
non-trivial.

From CLI or a startup script:

```
STACK 200000
```

Or wrap the run in a script that sets the stack:

```amiga-cli
Run >NIL: STACK 200000
filter_harness --tlv Game(2026-04-17).tlv --dump-header
```

### 6.5 Step 4 — Smoke Tests (in order)

Run these from the `FilterTest/` directory:

**T1 — Help text**
```
filter_harness --help
```
Expected: usage text printed, exit 0.

**T2 — Header dump (no profile, no scoring)**
```
filter_harness --tlv Game(2026-04-17).tlv --dump-header
```
Expected: format version 1, field map yes, CRC block yes, group_id field 0x05, data offset 422, file size 261573.

**T3 — CRC validation**
```
filter_harness --tlv Game(2026-04-17).tlv --defs defs --dump-crcs
```
Expected: `CSV CRC: OK  (13 files checked)`.

**T4 — Groups dump (no profile)**
```
filter_harness --tlv Game(2026-04-17).tlv --defs defs --dump-groups --limit 5
```
Expected: `Variant Groups: 2904 groups, 3973 variants  [group_id field]`, groups shown with
`[0001]`…`[0005]` format.

**T5 — Single group scoring**
```
filter_harness --tlv Game(2026-04-17).tlv --profile pal_aga_4mb.profile --defs defs --group 1869
```
Expected: 5 variants listed; `1869_v1.0_De_AGA_1653` selected with score 695.

**T6 — Full pipeline**
```
filter_harness --tlv Game(2026-04-17).tlv --profile pal_aga_4mb.profile --defs defs --out filter_results.txt
```
Expected: summary shows Selected: 2904, output file written.  Verify with:
```
List filter_results.txt
Type filter_results.txt
```

**T7 — Exclusion test**
```
filter_harness --tlv Game(2026-04-17).tlv --profile chipset_legacy_only.profile --defs defs --out filter_legacy.txt
```
Expected: Selected: 2832, Variants rejected: 234, Groups rejected: 72.

**T8 — Rejected dump**
```
filter_harness --tlv Game(2026-04-17).tlv --profile chipset_legacy_only.profile --defs defs --dump-rejected --limit 5
```
Expected: first five rejected group names printed.

### 6.6 Step 5 — Timing

If the Amiga build passes T1-T8, record wall-clock time for the full pipeline (T6):

```
echo "Start:"; Date; filter_harness --tlv Game(2026-04-17).tlv --profile pal_aga_4mb.profile --defs defs --out filter_results.txt; echo "End:"; Date
```

Reference timing targets based on prior `dat_to_tlv` benchmarks:

| CPU | Clock | Expected full-pipeline time |
|-----|-------|-----------------------------|
| 68000 | 7 MHz | < 30 s |
| 68030 | 40 MHz | < 3 s |
| 68040 | 40 MHz | < 1 s |

The filtering pipeline is read-only and does no CSV building — it should be significantly
faster than `dat_to_tlv` on the same hardware.

### 6.7 Step 6 — Diff Against Host Output

Copy `filter_results.txt` from Amiga back to host and diff against the reference:

```bat
fc /L output\filter_results_aga.txt <amiga-output>\filter_results.txt
```

An exact line-for-line match confirms that both platforms produce identical selections.
Any difference indicates an endian, alignment, or platform I/O issue that must be fixed
before the subsystem is considered Amiga-safe.

### 6.8 Known Crash Risk from AGENTS.md

The `dat_to_tlv` Amiga binary is known to crash.  If `filter_harness` also crashes:

1. Confirm `STACK 200000` is set before the run.
2. Run `T2` (header dump only) first — if this crashes, the crash is in TLV loading, not
   scoring.
3. Run `T4` (groups dump) — if this crashes, the crash is in variant scanning or qsort.
4. Run `T5` (one group) — if this crashes, the crash is in scoring.
5. Report which test crashes and attach a memory address if available.

If the crash is in `qsort`, consider replacing it with a simple insertion sort for the
Amiga build (`PLATFORM_AMIGA` guard) since `qsort` on vbcc is not guaranteed to be
stack-efficient.

---

## 7. Files Created or Modified in This Session

### New files (src_raw/filtering/)

| File | Stage | Description |
|------|-------|-------------|
| `tlv_filter.c` | I | Full pipeline wired into `whd_filter_run()` |
| `tlv_reader.c` | B | Binary load and header validation |
| `tlv_runtime.c` | B/C/patch | Field map, group map (0x02), and CRC block parsing; correct block parse order |
| `tlv_crc_validate.c` | D | CSV CRC validation (text-mode CRC) |
| `profile_binder.c` | E | Self-contained `.profile` INI loader and field binder |
| `tlv_variant.c` | F/patch | TLV record scanner → `WhdVariantView` array; group_id extracted, excluded from fields[] |
| `tlv_group.c` | G/patch | Dual-path grouper: group_id integer sort or base_name string fallback |
| `tlv_select.c` | H | Scoring engine and `tlv_select_run()` |
| `tlv_results.c` | I | Output file writer and console summary |

### New files (include_raw/filtering/)

| File | Notes |
|------|-------|
| `tlv_filter.h` | Public API, error codes, flags |
| `tlv_reader.h` | TlvReader struct and API |
| `tlv_runtime.h` | TlvRuntime, field map, CRC map, group map, `group_id_field_id`, `tlv_runtime_group_name()` |
| `tlv_crc_validate.h` | CRC validation API and result struct |
| `profile_binder.h` | WhdBoundField, WhdBoundProfile, API |
| `tlv_variant.h` | WhdTlvFieldValue, WhdVariantView (with `group_id`, `has_group_id`, `original_index`), WhdVariantArray |
| `tlv_group.h` | WhdVariantGroup (with `group_id`), WhdGroupSet (with `used_group_id_field`, `fallback_count`) |
| `tlv_select.h` | WhdSelectEntry, WhdSelectResult (`rejected_variants_count`, `rejected_groups_count`), WhdVariantScore |
| `tlv_results.h` | Write and summary API |

### Modified files

| File | Changes |
|------|---------|
| `tools/filter_harness/main.c` | CLI wired through all stages; `dump_header()` shows grouping mode; `dump_groups()` uses group map name and `[%04lu]` group_id format |
| `Makefile` | `filter_harness` target, `SRC_FH` source list, `gen_fixture_tlv` / `gen_fixtures` / `test_filter` targets |
| `include_raw/filtering/tlv_filter.h` | `WhdFilterResult`: split `rejected_count` → `rejected_variants_count` + `rejected_groups_count` |
| `include_raw/filtering/tlv_select.h` | `WhdSelectResult`: same split; `WhdVariantView`: added `has_group_id` |

---

## 8. Chunk 7 — Stage J: Regression Fixtures ✅

### 8.1 New Files

| File | Purpose |
|------|---------|
| `tools/gen_fixture_tlv/gen_fixture_tlv.c` | Standalone C99 generator that writes both TLV fixture files from scratch |
| `tests/filtering/defs/Chipset.csv` | Test CSV: AGA=1, OCS=2 (default) |
| `tests/filtering/defs/Language.csv` | Test CSV: En=1 (default), De=2 |
| `tests/filtering/defs/Memory.csv` | Test CSV: 1MB=1 (default), 512KB=2 |
| `tests/filtering/tiny_games.tlv` | 711-byte TLV — group_id field (0x05) + group map block (0x02) |
| `tests/filtering/tiny_games_fallback.tlv` | 575-byte TLV — no group_id, no group map |
| `tests/filtering/profile_aga_en.profile` | Filter: include AGA, exclude OCS; prefer En |
| `tests/filtering/profile_ocs_only.profile` | Filter: include OCS, exclude AGA — forces all-rejected groups |
| `tests/filtering/expected_aga_en.txt` | Golden output — 5 selected filenames |
| `tests/filtering/expected_ocs_only.txt` | Golden output — 2 selected filenames |
| `tests/filtering/run_tests.bat` | 4-test batch runner; exits 0 all pass, 1 any fail |

**New Makefile targets:** `gen_fixture_tlv`, `gen_fixtures`, `test_filter`.

---

### 8.2 Fixture Variant Data (11 variants, 5 groups)

| Group | ID | Variants | Notes |
|-------|----|----------|-------|
| AlienBreed | 1 | AGA_En, OCS_En, OCS_De | Main scoring case |
| Banshee | 2 | AGA_En, AGA_De | All-AGA — rejected group under ocs_only |
| CannonFodder | 3 | AGA_En, AGA_De | All-AGA — rejected group under ocs_only |
| DynaBlaster | 4 | v1.0a_AGA_En, v1.0b_AGA_En | **Tie case** — identical scores; first TLV entry wins |
| EaglesRider | 5 | AGA (no lang), OCS (no lang) | **Default-token case** — missing language falls back to CSV default En=1 |

---

### 8.3 Test Coverage

| Test | TLV | Profile | Expected result | Case covered |
|------|-----|---------|-----------------|--------------|
| T1 | `tiny_games.tlv` | `profile_aga_en` | 5 selected, 3 variants rejected, 0 groups rejected | group_id path, scoring, default token |
| T2 | `tiny_games.tlv` | `profile_ocs_only` | 2 selected, 8 variants rejected, 3 groups rejected | all-rejected groups, rejected_groups_count |
| T3 | `tiny_games_fallback.tlv` | `profile_aga_en` | identical to T1 | display_name heuristic fallback path |
| T4 | `tiny_games.tlv` | *(none)* | non-zero exit | strict CRC mismatch aborts before scoring |

---

### 8.4 Test Results

```
make test_filter
```

```
============================================================
 filter_harness regression tests
 Harness : build\host\filter_harness.exe
============================================================

[T1] tiny_games.tlv + profile_aga_en
  PASS
CSV CRC: OK  (3 files checked)
TLV     : tests\filtering\tiny_games.tlv
Profile : tests\filtering\profile_aga_en.profile
Variants: 11
Groups  : 5
Selected: 5
Variants rejected: 3
Groups rejected  : 0
Output  : build\host\t1_out.txt

[T2] tiny_games.tlv + profile_ocs_only
  PASS
CSV CRC: OK  (3 files checked)
TLV     : tests\filtering\tiny_games.tlv
Profile : tests\filtering\profile_ocs_only.profile
Variants: 11
Groups  : 5
Selected: 2
Variants rejected: 8
Groups rejected  : 3
Output  : build\host\t2_out.txt

[T3] tiny_games_fallback.tlv + profile_aga_en  (heuristic grouping)
  PASS
CSV CRC: OK  (3 files checked)
TLV     : tests\filtering\tiny_games_fallback.tlv
Profile : tests\filtering\profile_aga_en.profile
Variants: 11
Groups  : 5
Selected: 5
Variants rejected: 3
Groups rejected  : 0
Output  : build\host\t3_out.txt

[T4] CRC mismatch detection  (strict mode)
  PASS  (strict CRC correctly aborted with exit 1)
CSV CRC: FAILED
ERROR: CSV CRC mismatch: Chipset  tlv=347E60E6  current=9F614E7A

============================================================
 Results: 4 passed, 0 failed
============================================================
```

---

### 8.5 How the Generator Works

`gen_fixture_tlv.c` is a standalone C99 host tool.  It:

1. Opens each test CSV in text mode (`"r"`) and computes a CRC-32/ISO-HDLC — the same
   mode and algorithm as `tlv_crc_validate.c`, so embedded CRCs always match in T1–T3.
2. Assembles the TLV blocks in the correct order: field map (0x01) → group map (0x02,
   omitted for fallback) → CRC fingerprints (0x04) → data records.
3. Writes `group_id` entries as 2-byte big-endian payloads, matching the production
   writer.
4. Writes all data-record lengths and field-map sizes as 2-byte little-endian, matching
   the existing `tlv_runtime.c` reader.

To regenerate the fixtures after changing a test CSV:

```bat
make gen_fixtures
```

The Makefile `test_filter` target runs `gen_fixtures` automatically before running the
tests, so the TLV files and their embedded CRCs are always in sync.

---

### 8.6 All Plan Stages Now Complete

| Stage | Description | Status |
|-------|-------------|--------|
| A | Compile-only skeleton | ✅ |
| B | TLV load and header validation | ✅ |
| C | Field map, group map, CRC block parsing | ✅ |
| D | CRC validation against live CSVs | ✅ |
| E | Profile load and bind | ✅ |
| F | Variant view scan | ✅ |
| G | Grouping (group_id + fallback) | ✅ |
| H | Scoring and selection | ✅ |
| I | Full selection and output file | ✅ |
| J | Regression fixtures | ✅ |
