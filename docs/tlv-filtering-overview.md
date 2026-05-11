# TLV Runtime Filtering — Executive Overview

This document describes how the filtering subsystem consumes a pre-built TLV file, applies a
machine profile, and produces a filtered list of WHDLoad archive filenames.

---

## Purpose

The TLV file produced by `dat_to_tlv` is a self-describing binary index of WHDLoad game
metadata.  The filtering subsystem reads that file at runtime and answers one question:

> Given this machine's capabilities and language preferences, which archive or archives should be
> selected for each game group?
>
> By default the filter selects one archive per game group.  Profiles using slash buckets may
> select multiple archives from the same group — one per active selection lane.

The answer is a plain text file — one archive filename per line — ready to drive a WHDFetch
download or installation script.

All of the expensive work (DAT parsing, CSV loading, filename decoding, token resolution) was
done at TLV build time on the host PC.  The filtering runtime does none of that.  It loads a
compact binary, scores lightweight variant views in memory, and writes a single list.

---

## Stage 1 — Load and Validate the TLV

The first step is to load the entire TLV file into a single contiguous buffer.  A well-formed TLV
begins with a field-map block (type `0x01`).  If that block is absent the file is rejected
immediately.

Three header blocks are read in order:

**Block 0x01 — Field map.**  Maps each numeric field ID to its human-readable name.  The reader
does not hard-code field IDs; it resolves every name it needs (such as `display_name`,
`group_id`, `chipset`, `language`) from this map at startup.  This makes the filter forward-
compatible with any future field additions.

**Block 0x02 — Group map.**  An optional table mapping each numeric `group_id` to its canonical
group name (the game title without the variant suffix).  Old TLVs that predate this block are
handled gracefully; the runtime detects its absence and activates the fallback path in Stage 3.

**Block 0x04 — CSV fingerprints.**  The CRC-32 checksum of every CSV definition file that was
used when the TLV was built.  These are checked in Stage 2.

The byte position immediately following the last header block is recorded as `data_offset`.  All
subsequent reads for variant data start from that position.

**Endian reference table.**  The TLV format uses a mixed-endian convention.  All framing and
scalar fields are listed below for reference.

| Field | Width | Encoding |
|---|---|---|
| Record `value_length` | uint16 | **LE** |
| Block `payload_size` | uint16 | **LE** |
| Block entry `count` | uint16 | **LE** |
| CRC-32 fingerprint values (block 0x04) | uint32 | **LE** |
| Token IDs stored in data records | uint32 | **LE** |
| `group_id` value (field 0x05) | uint16 | **BE** |
| `archive_info` numeric fields (size\_kib, crc32) | uint32 | **BE** |

Structural framing fields and token IDs are little-endian because they were committed early
when the x86 builder wrote host-native integers directly.  The runtime applies explicit
`read_u16_le` / `read_u32_le` decodes so the encoding is unambiguous, not platform-dependent.
`group_id` and `archive_info` were added later with explicit big-endian encoding to match the
Motorola 68k native word order, allowing the Amiga runtime to read those two fields without
byte-swapping.  All other numeric fields require the LE decode.

---

## Stage 2 — Validate CSV Fingerprints

Before any filtering can begin the runtime checks that the CSV definition files on disk still
match the fingerprints embedded in the TLV.

For each entry in the CRC block the validator:

1. Builds the full path: `defs_dir / csv_name.csv`
2. Reads the file in text mode (matching the mode used by the builder on the same platform)
3. Computes a CRC-32/ISO-HDLC checksum
4. Compares it against the value stored in the TLV

A mismatch means the definition files have been edited since the TLV was built.  Token IDs
assigned during the build may no longer match the current CSV rows.  In strict mode (the default)
any mismatch aborts before scoring.  In warn-only mode the mismatch is recorded and reported but
filtering continues.

This check makes it impossible to silently score a game against the wrong lookup table.

---

## Stage 3 — Load and Bind the Profile

The `.profile` file describes the target machine: which chipsets are acceptable, which are
excluded, preferred languages, memory constraints, and the relative weight of each criterion.

The binder reads the profile's `[Filter.<fieldname>]` sections and resolves each token string
against the corresponding CSV file in `defs_dir`.  Resolution uses the same CSV lookup and
FNV-1a hash fallback that the TLV builder used, so both sides produce identical numeric IDs for
the same token string regardless of whether a CSV match is found.

The result is a compact `WhdBoundProfile` — an array of `WhdBoundField` records, one per active
filter field.  Each bound field carries:

- The TLV field ID (resolved from the field map in Stage 1)
- A 256-entry `rank_by_id` array for O(1) rank lookup during scoring
- The include and exclude token ID lists
- The field weight
- The CSV default token, if one is defined

Any profile field whose name does not appear in the TLV field map produces a warning and is
skipped.  A missing field in the TLV is not fatal; the field simply contributes nothing to any
variant's score.

---

## Stage 4 — Scan Variants

The runtime scans the TLV buffer from `data_offset` onwards, reading records in the wire format:

```
[field_id : 1 byte]  [value_length : 2 bytes LE]  [value : N bytes]
```

See the endian reference table in Stage 1 for the full list of field encodings.

Every `display_name` record (field ID `0x04`) starts a new variant.  Its value is the archive
filename stored as raw bytes without a NUL terminator.  The runtime allocates a NUL-terminated
copy and derives the canonical group name by scanning for the first `_v<digit>` pattern — for
example, `AlienBreed3_v1.0_AGA_En` → `AlienBreed3`.  This derived name is kept as a fallback for
old TLVs.

Interior records (all field IDs other than `display_name`) are attached to the current variant.
One field is treated specially:

**`group_id` (field ID `0x05`)** — a 2-byte big-endian uint16 (BE; see endian table).  When present it is stored
directly in `WhdVariantView.group_id` and excluded from the general field array so it never
inflates the `interior_fields` count or participates in profile scoring.  Its sole purpose is
to drive the grouper in Stage 5.

Each variant is assigned an `original_index` — its 0-based scan order — at the moment of
creation.  This index is runtime-only (never stored in the TLV) and is used as a deterministic
secondary sort key to preserve first-encountered TLV order within a group after sorting.

---

## Stage 5 — Group Variants

The grouper collects all variants that belong to the same logical game into a `WhdVariantGroup`.
It runs one of two paths depending on what the TLV contains:

**group_id path (new TLVs):**  Variants carry a numeric `group_id` set by the builder.  The
grouper sorts a companion index array by `(group_id ASC, original_index ASC)` and records
boundaries where the `group_id` changes.  The group name is resolved from the group map block
(Stage 1); if the block is absent or does not contain the ID, the fallback `base_name` string is
used instead.  Integer comparison makes grouping fast and unambiguous.

**Display-name fallback (old TLVs):**  When no `group_id` field exists in the field map the
grouper sorts by `(base_name ASC, original_index ASC)` — the canonical name derived in Stage 4.
String comparison produces the same logical groups as the integer path for any TLV whose entries
follow the standard `GameName_v<version>_<tags>` naming convention.

In both paths the sort is stable within a group because `original_index` is the secondary key.
First-encountered TLV order is preserved exactly, which is required by the tie-breaking rule.

---

## Stage 6 — Search Pre-Filter (optional)

If the caller supplies a search pattern, the runtime narrows the set of candidate game groups
before any scoring takes place.  Groups that do not match the pattern are excluded from Stage 7
entirely.

**This stage does not replace profile scoring.**  It only decides which groups are candidates.
Profile scoring in Stage 7 still selects the best variant inside each matched group.

### Matching rules

| Pattern | Behaviour |
|---|---|
| No `*` or `?` | Case-insensitive substring search — `lotus` matches `Lotus`, `Lotus2`, `Lotus3` |
| `*` | Matches zero or more characters |
| `?` | Matches exactly one character |

All matching uses ASCII case-folding only.  No locale-dependent functions are called.

Example:

```
--search lotus*
```

Filters candidate groups to those whose canonical name begins with `lotus` (case-insensitive),
then applies the selected profile to choose the best variant of each matched group:

```
Lotus
Lotus2
Lotus3
```

### New TLV path (group map present)

When block `0x02` is present and the TLV carries a `group_id` field, the pattern is matched
against the **canonical group name** from the group map (`tlv_runtime_group_name()`).  This
produces correct results even for groups whose individual variant filenames look quite different
from the canonical title.

### Old TLV fallback path (no group map)

When block `0x02` is absent the pattern is matched against the **base name** derived from
`display_name` by the `_v<digit>` heuristic used in Stage 5.  The fallback may produce slightly
different groupings from the canonical names for unusual filename formats, but it preserves
back-compatibility with TLVs built before the group map was introduced.

### Allow-list design

The result of the search is a compact `WhdGroupAllowList` — a flat byte array indexed by
0-based group index into the `WhdGroupSet`.  A group is either allowed (`1`) or not (`0`).

**`group_id` values are never modified by search.**  The allow list is a temporary runtime mask
only.  The original `group_id` assigned by the TLV builder remains unchanged throughout.

### No-match behaviour

If no groups match the pattern, Stage 7 receives an empty candidate set.  The output file is
valid but empty.  The harness prints `Selected: 0` and `Matched groups : 0`.  This is not
treated as an error and the harness exits `0`.

### Console summary (when search is active)

```
Search         : lotus*
Matched groups : 3
TLV     : output/Game(2026-04-17).tlv
Profile : assets_raw/profiles/pal_aga_4mb.profile
Variants: 3973
Groups  : 2904
Selected variants : 3
Selected groups   : 3
Variants rejected: 0
Groups rejected  : 0
Output  : output/filter_results.txt
```

---

## Stage 7 — Score and Select

For each group the scorer evaluates every variant against the bound profile and selects the
best variant for each active **selection lane**.

### Building the selection plan

Before any group is processed the profile binder produces a `WhdSelectionPlan`.  The plan is
derived from any `[Filter.<field>]` sections whose `include=` list contains a `/` separator.
Each `/`-separated chunk is a **bucket**; the lanes are the Cartesian product of all
bucket lists across slash-enabled fields.

Examples:

| Profile snippet | Slash fields | Lane count |
|---|---|---|
| `include=AGA,ECS,OCS` (chipset only) | 0 | 1 (implicit) |
| `include=AGA/ECS,OCS` (chipset only) | 1 | 2 |
| `include=AGA/OCS` + `include=En/De` | 2 | 2 × 2 = 4 |

A profile with no slash fields produces a single implicit lane whose selection criteria
are identical to the old single-winner behaviour.  Existing comma-only profiles are
never affected.

**Hard limits:** `FP_MAX_BUCKET_FIELDS=4` slash-enabled fields, `FP_MAX_BUCKETS_FIELD=8`
buckets per field, `FP_MAX_SELECTION_LANES=32` lanes total.  Exceeding any limit rejects
the profile at load time with a clear error; lanes are never silently truncated.

### Scoring a variant

For each bound field the scorer iterates over every field entry the variant carries with the
matching field ID.  Token ID values are stored as 4-byte little-endian uint32 (see endian table
in Stage 1).  For each token the scorer:

1. Checks the exclude list — an exclude hit immediately rejects the entire variant.
2. Computes the field score: `(include_count − rank) × weight` where `rank` is the position of
   the token in the include list (0 = highest priority).  An empty include list accepts all but
   scores 0.  A token absent from the include list scores 0 but does not reject.

The best field score across all values for that field is added to the variant's running total.
When no value is present for a field the CSV default token is used if one was defined.  After
all fields are evaluated the variant's `interior_fields` count is added as a small unconditional
bonus that favours variants with more metadata.

### Per-group selection

For each group:

1. **Rejection pre-pass.**  Every variant is scored globally once.  Variants that are
   excluded by any field are marked rejected and never reconsidered by any lane.

2. **Per-lane selection.**  For each lane the scorer scans all non-rejected variants and
   checks two additional conditions:
   - **Lane eligibility:** the variant must have an effective token that falls inside each
     bucket required by the lane.  The effective token may be an explicit TLV token or the
     field's CSV default token when the variant lacks that field entirely.  A lane with no
     requirements (single-lane plan) accepts every non-rejected variant.
   - **Bucket-local rank:** for fields that have a lane requirement, rank is computed
     relative to the bucket's own token list, not the full include list.  This ensures
     that ECS in `include=AGA/ECS,OCS` (where ECS is bucket 1, rank 0) outranks OCS
     within its own lane even though ECS has a lower global rank than AGA.
     For ordinary (non-lane) fields `include_count` and `rank` refer to the full include
     list.  For lane-required fields both values refer to the active bucket for that lane.
   - **Duplicate suppression:** a variant already selected for an earlier lane of the
     same group is skipped.  Each variant can appear at most once per group output.
     Lane order follows the profile: buckets run left-to-right as written, and the
     Cartesian product expands with the rightmost field varying fastest (lane 0 takes
     bucket 0 of every slash field, lane 1 advances the last slash field first, and so on).
   The highest-scoring eligible, non-duplicate variant is selected.  Ties are broken by
   first-encountered order (lowest `original_index`).

3. **Group accounting.**  A group contributes to `selected_groups` if at least one lane
   produced a winner.  All lane winners are counted in `total_selected_variants`.
   A group where every variant was rejected by an exclude rule contributes one to
   `rejected_groups_count` and produces no output lines.

### Backward compatibility

Comma-only profiles produce a plan with exactly one implicit lane and zero lane
requirements.  Every non-rejected variant is eligible for that lane and bucket-local rank
equals global rank.  The net result is identical to the previous single-winner behaviour.

---

## Stage 8 — Write the Output File

The output file contains one selected archive filename per line with no header.
When multiple selection lanes are active there may be several lines per game group —
one for each lane that found a winner:

```
AlienBreed_v1.0_AGA_En
AlienBreed_v1.0_OCS_En
Banshee_v1.0_AGA_En
CannonFodder_v1.0_AGA_En
...
```

Groups where all variants were excluded are silently skipped.  The file is a plain list suitable
for direct use by a download or installation script.

A summary is printed to the console.  The search line appears only when `--search` is active.
The `Selection lanes` line appears only when the profile has two or more lanes:

**Single-lane (comma-only) profile:**
```
CSV CRC: OK  (13 files checked)
TLV     : output/Game(2026-04-17).tlv
Profile : assets_raw/profiles/pal_aga_4mb.profile
Variants: 3973
Groups  : 2904
Selected variants : 2904
Selected groups   : 2904
Variants rejected: 0
Groups rejected  : 0
Output  : output/filter_results.txt
```

**Multi-lane (slash) profile:**
```
CSV CRC: OK  (13 files checked)
TLV     : output/Game(2026-04-17).tlv
Profile : assets_raw/profiles/multi_bucket_reference.profile
Variants: 3973
Groups  : 2904
Selected variants : 2996
Selected groups   : 2888
Selection lanes   : 4
Variants rejected: 155
Groups rejected  : 16
Output  : output/filter_results_multi.txt
```

---

## Key Design Decisions

**Nothing is decoded at runtime.**  The TLV file already contains pre-resolved numeric token IDs
for every metadata field.  The filter compares integers, not strings.  This is why the runtime
can be fast even on a 68000 at 7 MHz.

**The TLV is self-describing.**  The field map, group map, and CRC fingerprints are all embedded
in the file header.  The Amiga binary does not need field names or CSV tables baked in at compile
time.  A new TLV with extra fields can be read by an unmodified runtime binary.

**CRC validation is mandatory in strict mode.**  The embedded fingerprints exist to prevent a
class of silent error where definition files are updated on the host PC but the TLV is not
rebuilt.  Strict mode (the default) refuses to score against a stale index.

**Profile binding uses the same token IDs as the builder.**  When a profile token appears in a
CSV the binder records the CSV row ID.  When it does not, the same FNV-1a 8-bit hash fallback
used by the builder is applied.  Either way both sides produce the same number for the same
string, so scoring comparisons always work correctly.

**group_id separates structure from scoring.**  The `group_id` field is read into a dedicated
slot in the variant view and excluded from the general field array.  It cannot affect
`interior_fields` or any scoring calculation.  Grouping and scoring are completely independent
operations.

**Search is a group-level pre-filter, not a variant filter.**  The search pattern decides which
game groups are candidates.  All variants belonging to a matched group remain available for
scoring inside that group.  The filter never removes individual variants from a group before the
profile scorer has seen them, so the best variant is always chosen from the full group
membership.

**group_id is never altered by search.**  The allow-list produced by the search pre-filter is a
temporary runtime mask indexed by 0-based group position.  The `group_id` values assigned by
the TLV builder are read-only throughout the entire runtime lifetime.  They are not renumbered,
compacted, or rewritten.

**Search falls back gracefully on old TLVs.**  When block `0x02` is absent the pattern is
matched against the `base_name` string derived from `display_name` by the same heuristic the
grouper uses.  No code path requires both the group map and the `group_id` field to be present;
either one is sufficient for correct search behaviour.

**Mixed-endian convention is intentional and stable.**  Structural framing fields
(`value_length`, block `payload_size`, block `count`) and token ID values are little-endian
because they were frozen early when the x86 builder wrote host-native integers directly.
`group_id` and `archive_info` fields were added later with explicit big-endian encoding so the
Amiga runtime can read them without byte-swapping.  No further changes to the token-ID encoding
are planned: any future numeric payload introduced for Amiga-direct access should use explicit
big-endian encoding from the start; scalar framing fields remain LE.

**First-encountered wins on ties.**  The secondary sort key on `original_index` makes `qsort`
deterministic within a group.  The strict greater-than comparison in the selector (`score >
best_score`) means the first variant in TLV order wins when scores are equal.  No additional
tie-breaking logic is needed.

**Slash buckets produce independent lanes, not an ordered fallback.**  Each lane is evaluated
against the full set of non-rejected, non-duplicate variants.  A lane that finds no eligible
variant is silently skipped; no error is raised and no other lane is substituted.  This design
means the output for a group may have fewer lines than the total lane count when some lanes
have no matching variants (e.g. the target chipset simply does not exist for that game).

**Bucket-local rank keeps per-lane scoring fair.**  Within a lane, the rank of a token in a
bucket is its 0-based position inside that bucket, not its position in the full include list.
This prevents tokens in later buckets from being permanently outscored by tokens in earlier
buckets when they compete inside the same lane.

**Duplicate suppression is per-group.**  The set of already-selected variant indices is shared
across all lanes for a single group.  A variant selected by lane 0 is excluded from lanes 1
through N for that group.  The same variant can be selected in different groups without
restriction.

**The reusable subsystem has no I/O dependency.**  All printing is done by the harness caller.
The reusable `src_raw/filtering/` modules never write to stdout or stderr, making them safe to
embed in a real WHDFetch runtime that may manage its own display.
