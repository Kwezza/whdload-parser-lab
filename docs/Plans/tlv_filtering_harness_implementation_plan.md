# TLV Runtime Filtering Harness Implementation Plan

## 1. Purpose and Context

The `variant_backport_staging` project has reached the point where TLV creation is sufficiently optimised for now. The next phase is to consume those pre-built TLV files and use them to produce a filtered list of WHDLoad archive filenames for a chosen machine profile.

The existing `dat_to_tlv` tool converts RetroPlay / Logiqx-style DAT files into compact binary TLV files. The expensive work already happens during TLV creation: DAT parsing, filename decoding, CSV lookup, metadata extraction, archive size capture, archive CRC capture, field-map generation, and CSV fingerprint embedding.

This new phase must not repeat that work. It must load an existing TLV file, validate that the CSV definition files still match the CRC fingerprints embedded in the TLV, load a `.profile` file, score available variants, select the best variant per logical game, and write the selected archive filenames to a text file.

The implementation must be structured so that the core filtering/search code can later be lifted into a real WHDFetch runtime. The command-line harness is only a test wrapper and must stay separate from the reusable code.

Reference document for profile behaviour:

```text
PROJECT_ROOT\docs\profile_system.md
```

Use that file as the behavioural source of truth for:

- `.profile` file structure
- `[Filter.<fieldname>]` include/exclude semantics
- `[Scoring]` weight behaviour
- unknown field handling
- unresolved token handling
- default token handling
- scoring rules
- tie behaviour
- fallback behaviour

The TLV creation overview should also be used to understand the TLV file's purpose and design, especially the self-describing field map and embedded CSV CRC fingerprints.

---

## 2. High-Level Goal

Create a reusable TLV filtering subsystem and a separate test harness that can run a command similar to:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --defs assets_raw/defs \
  --packtypes assets_raw/prefs/pack_types.ini \
  --out output/filter_results.txt \
  --strict-crc
```

Expected behaviour:

1. Load the supplied TLV file.
2. Validate the TLV header and version.
3. Read the TLV field map.
4. Read the TLV embedded CSV CRC fingerprint block.
5. Validate the current CSV files against those embedded CRCs.
6. Load the supplied `.profile` file.
7. Bind profile fields to TLV field IDs.
8. Scan the TLV records into lightweight runtime variant views.
9. Group variants by logical game/base name.
10. Score each variant using the loaded profile.
11. Select the highest-scoring variant per game group.
12. Write one selected archive filename per line to the output file.
13. Print a short summary to the console.

---

## 3. Required Separation of Concerns

The reusable filtering code must live separately from the harness.

Set the project up like this unless testing proves the layout needs to be amended to fit the existing repository structure:

```text
src_raw/
  filtering/
    tlv_filter.h
    tlv_filter.c

    tlv_runtime.h
    tlv_runtime.c

    tlv_reader.h
    tlv_reader.c

    tlv_crc_validate.h
    tlv_crc_validate.c

    tlv_variant.h
    tlv_variant.c

    tlv_group.h
    tlv_group.c

    tlv_select.h
    tlv_select.c

    tlv_results.h
    tlv_results.c

tools/
  filter_harness/
    main.c
    README.md
```

Rules:

- `src_raw/filtering/` contains reusable WHDFetch-ready code.
- `tools/filter_harness/` contains command-line parsing, debug output, result-file writing, and test-only behaviour.
- The reusable filtering code must not depend on the harness.
- The reusable filtering code must not print directly unless an existing project logging abstraction already exists.
- The reusable filtering code must not contain test-only command-line argument parsing.
- The harness may call reusable debug/dump functions, but the core selector must remain usable without them.

---

## 4. Public Runtime API

Create a small public API in:

```text
src_raw/filtering/tlv_filter.h
```

Set up the public API like this unless testing proves it needs to be amended:

```c
typedef struct WhdFilterRequest {
    const char *tlv_name_or_path;
    const char *profile_path;
    const char *defs_dir;
    const char *pack_types_path;
    const char *output_path;
    unsigned int flags;
} WhdFilterRequest;

typedef struct WhdFilterResult {
    unsigned long total_variants;
    unsigned long total_groups;
    unsigned long selected_count;
    unsigned long rejected_count;
    unsigned long crc_mismatch_count;
    int had_warnings;
} WhdFilterResult;

int whd_filter_run(const WhdFilterRequest *request,
                   WhdFilterResult *result);
```

This may later be expanded to return an allocated list of selected entries rather than writing directly to a file. For the first harness milestone, it is acceptable for the harness to pass an output path, provided the file-writing responsibility remains isolated and can be removed or redirected later.

If returning results in memory is practical during this phase, use this shape:

```c
typedef struct WhdFilterSelectedEntry {
    const char *filename;
    const char *group_name;
    unsigned long score;
    unsigned short variant_index;
    unsigned short flags;
} WhdFilterSelectedEntry;

typedef struct WhdFilterOutputList {
    WhdFilterSelectedEntry *entries;
    unsigned long count;
} WhdFilterOutputList;
```

Also provide:

```c
void whd_filter_free_results(WhdFilterOutputList *list);
const char *whd_filter_error_string(int error_code);
```

The exact error-code values can follow the existing project style.

---

## 5. Build and Coding Constraints

The reusable code must remain Amiga-oriented and C89-friendly.

Requirements:

- Use C89-compatible declarations.
- Do not declare variables inside `for` loop initialisers.
- Do not use variable length arrays.
- Avoid unnecessary dynamic allocation inside tight loops.
- Avoid recursion.
- Treat all TLV disk values as big-endian / Motorola order.
- Host builds must byte-swap where needed.
- Keep the Amiga path simple and direct.
- Do not add clever memoisation, extra lookup layers, or branch-heavy caches unless benchmarks prove they help.
- Prefer scanning compact arrays over pointer-heavy linked structures.
- Keep the TLV buffer owned by the reader/runtime layer.
- Point runtime views into the loaded TLV buffer where safe.
- Copy strings only when necessary.

The project benchmark history shows that extra branchy lookup layers can regress performance on 68000/68030-class CPUs. Optimise for simple, predictable passes over compact data.

---

## 6. Important Behavioural Rules

### 6.1 Do Not Parse DAT Files

The runtime filtering phase must not parse DAT files. It consumes TLV files only.

The DAT was already processed by `dat_to_tlv`. The runtime should rely on the TLV records produced by that process.

### 6.2 Do Not Decode Filenames Again

The runtime filtering phase must not redo filename tokenisation or metadata extraction.

Filename-derived metadata should already exist in the TLV as structured field/value records.

### 6.3 Use the TLV Field Map

The TLV contains a field map. Use it.

Do not hard-code assumptions such as:

```text
chipset is always field ID 4
language is always field ID 5
memory is always field ID 6
```

Instead:

```text
profile field name -> field registry -> TLV field map -> runtime field ID
```

### 6.4 Validate CSV CRCs Before Scoring

CSV CRC validation must happen before profile binding and scoring.

If the CSV definitions have changed since the TLV was created, token IDs may no longer mean the same thing. In strict mode this must abort filtering.

### 6.5 Reuse the Existing Profile System

Do not invent a second profile format.

Use the existing profile behaviour documented in:

```text
docs\profile_system.md
```

Reuse the existing loader/scoring structures where practical.

---

## 7. CRC Validation Behaviour

Create:

```text
src_raw/filtering/tlv_crc_validate.h
src_raw/filtering/tlv_crc_validate.c
```

The TLV contains the CRC-32 fingerprint of every CSV file used during TLV creation. The validator must compare those embedded fingerprints against the current files in `assets_raw/defs`.

Validation flow:

```text
for each CSV fingerprint embedded in TLV:
    build path: defs_dir + csv_name
    load raw CSV bytes
    compute CRC-32
    compare against TLV stored CRC
```

Support at least these result states:

```text
OK
CSV missing
CSV unreadable
CRC mismatch
TLV has no CRC block
```

Support flags similar to:

```c
#define WHD_FILTER_CRC_STRICT   0x0001
#define WHD_FILTER_CRC_WARNONLY 0x0002
```

Default harness behaviour should be strict CRC validation.

Strict mode:

```text
Any missing, unreadable, or mismatched CSV aborts before scoring.
```

Warning-only mode:

```text
Warnings are recorded and printed by the harness, but filtering may continue.
```

---

## 8. Profile Loading and Binding

The existing profile system loads `.profile` files from:

```text
assets_raw/profiles/
```

A profile contains:

```text
[Profile]
[Filter.<fieldname>]
[Scoring]
```

The runtime filtering subsystem must load the requested profile and bind it to the TLV's actual field map.

Binding process:

```text
for each active profile field:
    look up field name in the field registry
    look up the same field name in the TLV field map
    resolve profile include/exclude token IDs
    record the TLV field ID needed for runtime matching
```

Create a compact runtime binding structure similar to:

```c
typedef struct WhdBoundField {
    unsigned short tlv_field_id;
    const FP_FieldProfile *profile_field;
    unsigned char weight;
    unsigned char has_default;
    unsigned short default_token_id;
} WhdBoundField;
```

Adjust names and types to match the existing codebase.

Unknown profile fields must follow the existing profile-system behaviour:

```text
- skip the unknown section or weight
- set warning state
- continue loading
- do not crash
- do not abort unless the existing loader already treats the case as fatal
```

---

## 9. TLV Reader and Runtime Views

Create:

```text
src_raw/filtering/tlv_reader.h
src_raw/filtering/tlv_reader.c
src_raw/filtering/tlv_runtime.h
src_raw/filtering/tlv_runtime.c
```

### 9.1 TLV Reader Responsibilities

`tlv_reader.c` should handle raw binary loading and basic structural validation:

```text
- load whole TLV file into memory
- validate magic/header/version/endian marker
- expose top-level TLV record spans
- provide safe big-endian read helpers
- release the loaded buffer
```

### 9.2 TLV Runtime Responsibilities

`tlv_runtime.c` should build lightweight views over the loaded TLV:

```text
- field map
- CSV CRC fingerprint map
- variant records
- string references
- archive_info references if present
```

The runtime view should not duplicate large amounts of data. It should point into the loaded TLV buffer where safe.

---

## 10. Variant Views

Create:

```text
src_raw/filtering/tlv_variant.h
src_raw/filtering/tlv_variant.c
```

Each TLV archive record should become a lightweight variant view.

Set it up like this unless testing proves the TLV layout requires a different shape:

```c
typedef struct WhdVariantView {
    const char *filename;
    const char *base_name;
    unsigned short variant_index;
    unsigned short field_count;
    unsigned short interior_fields;
    const WhdTlvFieldValue *fields;
} WhdVariantView;
```

Required variant operations:

```text
- get filename
- get canonical/base game name
- get interior_fields count
- find field by TLV field ID
- enumerate values for a field
- read archive_info if needed later
```

The first implementation can build an array of variant views after loading the TLV.

---

## 11. Grouping Variants Into Games

Create:

```text
src_raw/filtering/tlv_group.h
src_raw/filtering/tlv_group.c
```

The selector must choose one best variant per logical game, not one result per archive.

Preferred grouping source:

1. Use a canonical/base game key stored in the TLV.
2. If no dedicated field exists, use the most stable existing TLV field that represents the base game name.
3. Only fall back to deriving a group name from the archive filename if no better TLV source exists.

Long-term rule:

```text
Grouping should be based on data created by dat_to_tlv, not by reparsing filenames in the runtime filter.
```

Use a compact structure similar to:

```c
typedef struct WhdVariantGroup {
    const char *group_name;
    unsigned short first_variant;
    unsigned short variant_count;
} WhdVariantGroup;
```

Implementation guidance:

- Build a sorted array of variant indexes grouped by base name.
- Avoid one linked list per group.
- Preserve original TLV order within a group unless a later test proves a different order is required.
- Tie behaviour must remain first-encountered wins.

---

## 12. Scoring and Selection

Create:

```text
src_raw/filtering/tlv_select.h
src_raw/filtering/tlv_select.c
```

The selector should operate on:

```text
- runtime TLV view
- bound profile
- grouped variant list
```

Selection flow:

```text
for each group:
    best_variant = none
    best_score = 0

    for each variant in group:
        score variant using bound profile

        if variant is rejected:
            rejected_count++
            continue

        if no best variant yet:
            best_variant = current
            best_score = score
            continue

        if score > best_score:
            best_variant = current
            best_score = score

    if best_variant exists:
        emit selected filename
```

Required scoring behaviour from `docs\profile_system.md`:

```text
- exclude match rejects the whole variant
- include list order controls rank and score
- empty include means accept all
- token not in include list scores zero but does not reject
- missing field may use CSV default token
- weight 0 disables scoring but exclusions still apply
- interior_fields is added as a small unconditional bonus
- first-encountered variant wins ties
```

Do not put command-line output inside the selector. The selector should return status and data to the caller.

---

## 13. Result Output

Create:

```text
src_raw/filtering/tlv_results.h
src_raw/filtering/tlv_results.c
```

For the first milestone, the harness should write a plain text file containing one selected archive filename per line:

```text
AlienBreed2_v1.0_AGA_En.lha
BadDudesVsDragonNinja_v1.01_AGA.lha
...
```

Add a small header only if it does not interfere with downstream testing. If the result will be compared using simple diff tools, keep the file filename-only.

Console summary should include:

```text
TLV: output/Games(19-05-2025).tlv
Profile: assets_raw/profiles/pal_aga_4mb.profile
CSV CRC: OK
Variants scanned: 3861
Groups found: <n>
Selected: <n>
Rejected: <n>
Warnings: <n>
Output: output/filter_results.txt
```

Later, an extended result format may include:

```text
filename,size_kib,crc32,score,group
```

Do not add the extended CSV format until the plain filename output is working.

---

## 14. Harness Command-Line Interface

Create:

```text
tools/filter_harness/main.c
```

Minimum command:

```text
filter_harness <tlv-file> <profile-file> <output-file>
```

Preferred full command:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --defs assets_raw/defs \
  --packtypes assets_raw/prefs/pack_types.ini \
  --out output/filter_results.txt \
  --strict-crc
```

Useful debug flags to add as the implementation progresses:

```text
--dump-header
--dump-fields
--dump-crcs
--dump-profile
--dump-groups
--dump-rejected
--group <name>
--limit <n>
--warn-crc
--strict-crc
```

Only implement debug flags when they directly help the current stage. Do not spend time building a large CLI before the core runtime path works.

---

## 15. Implementation Stages

Work through these stages one at a time. Do not skip ahead to scoring before TLV loading, CRC validation, and profile binding are proven.

### Stage A: Compile-Only Skeleton

Create the folder structure, headers, empty implementation files, and harness entry point.

Deliverable:

```text
filter_harness --help
```

Acceptance criteria:

```text
- project builds on host
- harness prints basic usage
- reusable filtering folder is compiled
- no real TLV loading yet
```

---

### Stage B: TLV File Load and Header Validation

Implement:

```text
tlv_reader_load()
tlv_reader_free()
tlv_reader_validate_header()
```

Deliverable:

```text
filter_harness --tlv output/Games(19-05-2025).tlv --dump-header
```

Acceptance criteria:

```text
- valid TLV loads successfully
- invalid/missing TLV fails cleanly
- header/version/endian information is reported
- no memory leak on success or failure
```

---

### Stage C: Field Map and CRC Block Dump

Implement parsing for:

```text
- TLV field map
- embedded CSV CRC fingerprint block
```

Deliverable:

```text
filter_harness --tlv output/Games(19-05-2025).tlv --dump-fields --dump-crcs
```

Acceptance criteria:

```text
- field names and numeric IDs are listed
- CSV filenames and embedded CRCs are listed
- missing optional blocks are handled cleanly
```

---

### Stage D: CRC Validation Against assets_raw/defs

Implement current CSV validation.

Deliverable:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --defs assets_raw/defs \
  --dump-crcs \
  --strict-crc
```

Acceptance criteria:

```text
- matching CSVs report OK
- missing CSVs report a clear error
- mismatched CRCs report both TLV CRC and current CSV CRC
- strict mode aborts before scoring
- warning-only mode can continue
```

---

### Stage E: Profile Load and Bind

Load the supplied `.profile` file and bind its fields to TLV field IDs.

Deliverable:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --packtypes assets_raw/prefs/pack_types.ini \
  --defs assets_raw/defs \
  --dump-profile
```

Acceptance criteria:

```text
- profile loads using existing profile-system behaviour
- chipset/language/memory filters bind to TLV field IDs when present
- unknown profile fields warn and continue
- unresolved tokens behave as documented in profile_system.md
- profile warnings are surfaced in the harness summary
```

---

### Stage F: Variant View Scan

Parse TLV archive records into lightweight `WhdVariantView` entries.

Deliverable:

```text
filter_harness --tlv output/Games(19-05-2025).tlv --limit 10
```

Acceptance criteria:

```text
- total variant count is correct
- first few filenames can be printed
- field counts can be inspected
- interior_fields can be read
- archive_info can be located if present, but does not need to affect scoring yet
```

---

### Stage G: Grouping

Group variants by logical game/base name.

Deliverable:

```text
filter_harness --tlv output/Games(19-05-2025).tlv --dump-groups --limit 20
```

Acceptance criteria:

```text
- multiple variants of the same game group together
- unrelated games do not group together
- original within-group order is preserved
- group count is printed
```

If the TLV does not currently contain a suitable canonical grouping key, document that finding clearly and implement the least risky temporary fallback. Also add a note that `dat_to_tlv` should later emit an explicit group/base-name field.

---

### Stage H: Score One Group

Add a debug path to score a single group.

Deliverable:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --defs assets_raw/defs \
  --packtypes assets_raw/prefs/pack_types.ini \
  --group AlienBreed
```

Acceptance criteria:

```text
- each variant in the group shows a score
- rejected variants are identified
- selected variant is shown
- score behaviour matches profile_system.md
```

---

### Stage I: Full Selection and Output File

Run the selector across all groups and write the output file.

Deliverable:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --defs assets_raw/defs \
  --packtypes assets_raw/prefs/pack_types.ini \
  --out output/filter_results.txt \
  --strict-crc
```

Acceptance criteria:

```text
- output file is created
- one selected filename is written per group
- summary counts are printed
- repeated runs produce stable output
- tie behaviour is first-encountered wins
```

---

### Stage J: Regression Fixtures

Add small regression fixtures if the repository already has a suitable test structure.

Suggested layout:

```text
tests/filtering/
  tiny_games.tlv
  profile_aga_en.profile
  expected_aga_en.txt
  profile_ocs_only.profile
  expected_ocs_only.txt
```

Acceptance criteria:

```text
- small fixture can be run quickly
- expected output can be diffed against actual output
- unknown profile field case is covered
- CRC mismatch case is covered if practical
- tie case is covered if practical
```

---

## 16. Error Handling Requirements

Errors should be explicit and actionable.

Examples:

```text
ERROR: unable to open TLV file: output/Games(19-05-2025).tlv
ERROR: invalid TLV header
ERROR: unsupported TLV version: 3
ERROR: missing CSV: assets_raw/defs/Chipset.csv
ERROR: CSV CRC mismatch: Chipset.csv tlv=12345678 current=9ABCDEF0
ERROR: unable to load profile: assets_raw/profiles/pal_aga_4mb.profile
ERROR: profile could not be bound to TLV field map
ERROR: no variants found in TLV
ERROR: no groups produced from TLV
```

Warnings should be visible but not fatal unless strict behaviour requires it.

Examples:

```text
WARNING: unknown profile field skipped: fakechipset
WARNING: TLV has no CRC fingerprint block
WARNING: profile loaded with warnings
WARNING: falling back to filename-derived grouping because no TLV group key was found
```

---

## 17. Archive Size and CRC Handling

The TLV creation pipeline now carries archive size and archive CRC from the DAT `<rom />` entries into an `archive_info` TLV field.

For this filtering phase:

- Parse and expose `archive_info` if the TLV contains it.
- Do not use archive size or archive CRC for scoring.
- Do not make archive_info mandatory for initial selection.
- Prepare the result-entry structure so size and CRC can be added to output later.

Later optional extended output:

```text
filename,size_kib,crc32,score,group
```

Initial output remains filename-only.

---

## 18. Performance Guidance

This phase should be much faster than TLV creation because it must not parse DAT files or decode filenames.

Performance priorities:

1. Load TLV once.
2. Build compact runtime views.
3. Bind profile fields once.
4. Scan variants once.
5. Group variants once.
6. Score variants using simple loops.
7. Avoid allocations inside per-variant scoring.
8. Avoid repeated string comparisons in hot paths where numeric IDs are available.
9. Avoid linked-list-heavy designs.
10. Keep the first version correct and simple before optimising.

Do not optimise prematurely. If a change adds another lookup structure, cache, or memo table, benchmark it before keeping it.

---

## 19. First Complete Milestone

The first complete milestone is this command working end-to-end:

```text
filter_harness \
  --tlv output/Games(19-05-2025).tlv \
  --profile assets_raw/profiles/pal_aga_4mb.profile \
  --defs assets_raw/defs \
  --packtypes assets_raw/prefs/pack_types.ini \
  --out output/filter_results.txt \
  --strict-crc
```

Required final output:

```text
output/filter_results.txt
```

Containing:

```text
<one selected WHDLoad archive filename per logical game>
```

Required console summary:

```text
TLV loaded: yes
CSV CRC validation: OK
Profile loaded: yes
Profile warnings: none or count
Variants scanned: <n>
Groups found: <n>
Selected: <n>
Rejected: <n>
Output file: output/filter_results.txt
```

---

## 20. Notes for the AI Agent

Work phase by phase.

After each phase:

1. Build the host version.
2. Run the smallest useful test.
3. Record what works.
4. Record any assumptions made.
5. Do not continue to the next phase until the current phase has a visible result.

When the existing codebase disagrees with this plan, prefer the existing codebase and document the adjustment.

When `docs\profile_system.md` disagrees with this plan, prefer `docs\profile_system.md` for profile behaviour.

When the TLV layout disagrees with assumptions in this plan, inspect the TLV writer and update the reader to match the writer. Do not change the TLV writer unless the runtime cannot reasonably consume the current format.

Keep the final design focused on the real goal:

```text
A small, reusable WHDFetch-ready filtering engine that can load a pre-built TLV, apply a machine profile, and return the best archive filename for each game.
```
