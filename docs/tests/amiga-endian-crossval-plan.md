# Amiga Endianness Cross-Validation Test Plan

**Created:** 2026-05-27
**Status:** Phase 4 complete — Amiga device run passed (2026-05-27); all 34 assertions passed

---

## Purpose

The endianness remediation work (Items 1–12 in
[endianness-divergence.md](../endianness-divergence.md)) has been completed and verified
on the host (x64/Windows, GCC).  This plan describes the Amiga-side validation that must
be completed before the remediation can be considered fully closed.

The tests must confirm that a TLV file built by the PC tool is read and filtered correctly
by the Amiga binary — i.e. that the uniform big-endian on-disk format is decoded without
corruption by the Amiga runtime.

---

## Standalone Constraint

The test program must be **entirely self-contained**.  It must:

- Compile and link as its own executable — not as part of the main `dat_to_tlv` binary
  or any other production binary.
- Include **only** `whdtlv/whdtlv.h` (the public facade header).  No internal headers.
- Require **zero changes** to any production source file.
- Be C89-compatible so vbcc can build it without modification.
- Mirror the same pattern already established by
  `tests/filtering/test_filter_facade.c`.

The test file is:

```
tests/filtering/test_amiga_endian.c
```

It is built as a separate Makefile target (see [Makefile rules](#makefile-rules) below).

---

## What the Tests Must Prove

The three categories of evidence required:

| Category | What is at risk | How detected |
|---|---|---|
| Block framing | `map_size`, `group_count`, `payload_size`, CRC-32 values in block headers | Wrong group/variant totals; `WHDTLV_FILTER_ERR_CRC` return |
| Token ID values | CSV-backed uint32 field values written and read as BE | Correct variant *name* selected — not just a non-zero count |
| Multi-pack coverage | Every pack type uses the same block framing | Load and count-check across two different TLV types |

---

## Input Files Required on the Amiga

The following files must be copied to the Amiga alongside the test binary.  All paths are
relative to the directory from which the test binary is run.

```
output/Game(2026-04-17).tlv
output/Mags(2025-07-24).tlv
assets_raw/defs/           (full directory)
assets_raw/profiles/pal_aga_4mb.profile
assets_raw/profiles/multi_bucket_reference.profile
```

The TLV files in `output/` were regenerated during Item 11 of the endianness remediation
and contain uniform big-endian encoding.  Do not use any TLV file produced before
2026-05-27.

---

## Host Baselines

**Do not reuse historical counts.**  The values used in the test assertions must be
captured from the current host binary against the exact TLV files that will be copied
to the Amiga.  Run the host filter facade test and record all five numbers:

```bat
build\host\test_filter_facade.exe
```

From the line printed by Test 1:

```
(variants=NNNN groups=GGGG selected=SSSS lanes=LL)
```

and from Test 4 (multi-lane):

```
(lanes=LL selected=SS)
```

Record:

| Symbol | Meaning | Used in |
|---|---|---|
| `HOST_GAME_VARIANTS` | `variants_total` for Game TLV | T10 |
| `HOST_GAME_GROUPS`   | `groups_total`   for Game TLV | T10 |
| `HOST_GAME_SELECTED` | `selected_variants` under `pal_aga_4mb.profile`, no search | T11 |
| `HOST_MAGS_GROUPS`   | `groups_total`   for Mags TLV | T14 |
| `HOST_MULTI_LANES`   | `selection_lanes` under `multi_bucket_reference.profile` | T15 |

For `HOST_MAGS_GROUPS`, run separately:

```c
/* temporary host-only check — not part of the Amiga test */
whdtlv_filter_to_list("output/Mags(2025-07-24).tlv", DEFS_DIR, NULL, NULL, ...);
printf("Mags groups_total=%u\n", summary.groups_total);
```

or read it from the `make run` output in Item 11 of the remediation log
(`Mags 104 entries`) — but confirm it against the current binary before hardcoding.

---

## Test Descriptions

### Test T10 — Group count matches host baseline

**Purpose:** Confirms that block `0x02` `group_count` and block `0x01` `map_size` are
decoded correctly as BE values.  If either is byte-swapped, the group scan will
mis-count or read garbage.

```
TLV:     output/Game(2026-04-17).tlv
Profile: NULL (default scoring)
Search:  none
Assert:
  rc == WHDTLV_OK
  summary.variants_total == HOST_GAME_VARIANTS   /* capture from host first */
  summary.groups_total   == HOST_GAME_GROUPS     /* capture from host first */
```

Asserting both values makes the test diagnostic: if block parsing is wrong, the failure
message will indicate whether the variant scan or the group map phase failed.

---

### Test T11 — Selected variant count matches host baseline

**Purpose:** Confirms that token ID field values (uint32, block framing, and
`value_length`) all decode correctly.  If any of these are byte-swapped, the scorer
will read wrong token IDs and may select different variants.

```
TLV:     output/Game(2026-04-17).tlv
Profile: assets_raw/profiles/pal_aga_4mb.profile
Search:  none
Assert:
  rc == WHDTLV_OK
  summary.selected_variants == <HOST_BASELINE>   /* capture from host first */
  summary.selection_lanes == 1
```

---

### Test T12 — Host-oracled fixture parity

**Purpose:** This is the strongest token-ID correctness test.  The host binary is run
against the exact same TLV and profile files that the Amiga will use.  A set of 5–8
game groups is selected that covers useful variant diversity.  The host-selected
filenames are recorded exactly and embedded in the Amiga test source as static string
arrays.  The Amiga test then runs the same (search, profile) pairs and asserts exact
result-list parity.

This is a parity test, not a game-knowledge test.  Nothing is assumed about which
groups contain AGA, English, or any other token.  The host output is the oracle.

#### Fixture Discovery Step

Run `build\host\filter_harness.exe` for each candidate (search, profile) pair and
examine the output.  Select fixtures that satisfy these coverage criteria collectively
across the 5–8 chosen groups:

| Coverage goal | What to look for in host output |
|---|---|
| Chipset token ID | A group where the profile visibly prefers one chipset variant over another (inspect that both a preferred and a non-preferred variant appear under a search result so you can confirm the winner changed when a different profile is used) |
| Language token ID | A group where at least two language variants are present and the profile's language weights change which one wins |
| Memory token ID | A group where memory variants are present (if any exist in the TLV) |
| Multi-lane selection | A (search, multi-lane profile) pair that returns more than one result — one per lane |
| No-match search | A search pattern that returns zero results and `matched_groups == 0` |
| Second profile | At least one group run with a second profile (e.g. `chipset_aga_only.profile`) so that both `pal_aga_4mb.profile` and `chipset_aga_only.profile` token-ID decode paths are exercised |

Do not invent or guess patterns.  Use the host output to identify real groups that
satisfy the criteria above.

The discovery command for a single fixture is:

```bat
build\host\filter_harness.exe "output\Game(2026-04-17).tlv" ^
    "assets_raw\defs" "assets_raw\profiles\pal_aga_4mb.profile" ^
    "<search_pattern>" "build\host\fixture_out.txt" "build\host\fixture_summary.txt"
type build\host\fixture_out.txt
```

Quote all path arguments — the TLV filename contains parentheses which the command
interpreter would otherwise treat as subexpression delimiters.  Use a single caret `^`
for line continuation in a `.bat` file or interactive prompt; doubled `^^` is only
needed inside some nested `cmd /C` contexts and creates ambiguity here.

For a no-match fixture, confirm that `fixture_out.txt` is empty and the summary shows
`matched_groups: 0`.

**Diversity verification:** The filtered output file only shows the winner for each
group — it does not show the non-selected variants.  To confirm that a candidate fixture
actually exercises chipset or language preference (rather than selecting the only variant
present), use the profile-aware report tool or a wide CSV export:

```bat
build\host\whdtlv_report.exe "output\Game(2026-04-17).tlv" ^
    "assets_raw\defs" "assets_raw\profiles\pal_aga_4mb.profile" ^
    "<search_pattern>" "build\host\fixture_report.csv"
```

Open `fixture_report.csv` and confirm the matched group contains at least two variants
with different chipset, language, or memory tokens before accepting it as a diversity
fixture.  Do not rely on the filtered output file alone to prove diversity.

#### Fixture Storage

Each confirmed fixture is saved as a plain text file — one selected filename per line —
in:

```
tests/filtering/expected/endian/
```

Naming convention: `<search_slug>_<profile_slug>.txt`.  For example:

```
tests/filtering/expected/endian/f01_SEARCHSLUG_pal_aga_4mb.txt
tests/filtering/expected/endian/f02_SEARCHSLUG_pal_aga_4mb.txt
tests/filtering/expected/endian/f03_SEARCHSLUG_chipset_aga_only.txt
tests/filtering/expected/endian/f04_SEARCHSLUG_multi_bucket.txt   <- multi-lane case
tests/filtering/expected/endian/f05_NOMATCH_pal_aga_4mb.txt       <- empty file
```

**Fixture ordering is significant.**  Do not sort either side before comparison.  The
Amiga must reproduce the host-selected list in exactly the same order.  Order
differences expose grouping, search, and tie-break regressions as well as token-ID
failures.

Alongside each fixture text file, save a matching `.summary` sidecar with the key
counts from the host run:

```
tests/filtering/expected/endian/f01_SEARCHSLUG_pal_aga_4mb.summary
```

Format (plain text, one key=value per line):

```
profile=pal_aga_4mb.profile
search=<search_pattern>
tlv=Game(2026-04-17).tlv
matched_groups=...
selected_variants=...
selected_groups=...
selection_lanes=...
```

The Amiga test can optionally assert summary counters as well as filenames; at minimum
the sidecar documents the host context so a future reader can understand what the
fixture is testing without re-running the host tool.

These files are the committed ground truth.  They are generated once from the host and
not modified unless the TLV or profile changes.

#### Embedding in the Test Source

Because the Amiga test binary must be standalone (no file I/O for expected data), the
fixture filenames are embedded directly as static string arrays in
`tests/filtering/test_amiga_endian.c`.  A conversion step reads each fixture file and
produces the array literal:

```c
/* Fixture F01: <search_slug> under pal_aga_4mb.profile
 * Source: tests/filtering/expected/endian/f01_SEARCHSLUG_pal_aga_4mb.txt
 * Generated by host on <date> from output/Game(2026-04-17).tlv
 * matched_groups=N selected_variants=N selection_lanes=1               */
static const char *f01_expected[] = {
    "ExactFilename_v1.2_AGA",
    NULL
};
static const unsigned int f01_count = 1u;

/* Fixture F05: no-match search */
static const char *f05_expected[] = { NULL };
static const unsigned int f05_count = 0u;
```

**vbcc compatibility note:** `static const char * const arr[]` (pointer-to-const,
const pointer) is standard C89 but some older vbcc configurations warn or error on the
double `const` in array declarations.  If vbcc rejects it, drop the second `const` and
write `static const char *arr[]`.  This loses a little const-correctness on the pointer
itself but keeps the test portable without any functional change.

The conversion can be done manually (copy each line from the fixture file into the
array) or with a small host-side script.  The result is checked in alongside the
fixture text files so the expected values are visible in code review without running
the host tool.

#### Assertion Logic

For each fixture the test performs:

```
1. Run whdtlv_filter_to_list() with (search, profile, TLV, defs).
2. Assert rc == WHDTLV_OK.
3. Assert results.count == fixture.expected_count.
4. For each i in 0..expected_count-1:
       Assert results.items[i] != NULL.
       Assert strcmp(results.items[i], fixture.expected[i]) == 0.
5. Free results.
```

For the no-match fixture:

```
1. Run whdtlv_filter_to_list().
2. Assert rc == WHDTLV_OK.
3. Assert results.count == 0.
4. Assert summary.matched_groups == 0.
```

The only helper needed inside the test file is a comparison loop — no substring search:

```c
static int results_match_expected(
    const WhdTlvStringList *r,
    const char * const     *expected,
    unsigned int            expected_count)
{
    unsigned int i;

    if (r == NULL || expected == NULL) {
        return 0;
    }
    if (r->count != expected_count) {
        return 0;
    }
    for (i = 0u; i < expected_count; ++i) {
        if (r->items == NULL || r->items[i] == NULL || expected[i] == NULL) {
            return 0;
        }
        if (strcmp(r->items[i], expected[i]) != 0) {
            return 0;
        }
    }
    return 1;
}
```

Do not call this helper for the no-match fixture (`expected_count == 0`); use the
separate no-match assertion path instead, which checks `results.count == 0` and
`summary.matched_groups == 0` directly.  The helper does not handle the
`items == NULL` / `count == 0` case as a positive match.

`strcmp()` is C89.  This helper is local to the test file and does not touch any
production source.

---

### Test T13 — CRC fingerprint validation passes

**Purpose:** Block `0x04` stores CRC-32 values for each CSV definition file.  These
were written as BE uint32 by the builder (Item 3) and must be read as BE by the Amiga
runtime (Item 6).  A mismatch returns `WHDTLV_FILTER_ERR_CRC` or sets
`summary.crc_mismatches > 0`.

```
TLV:     output/Game(2026-04-17).tlv
Profile: assets_raw/profiles/pal_aga_4mb.profile
Search:  none
Assert:
  rc == WHDTLV_OK                     /* ERR_CRC (-5) would appear here */
  summary.crc_mismatches == 0
  summary.crc_files_checked > 0      /* confirms the CRC path was exercised */
```

This is partly covered by T11, but the explicit `crc_mismatches == 0` assertion isolates
the block-framing BE read for `crc32` values specifically.

---

### Test T14 — Mags pack type loads with correct group count

**Purpose:** All five pack types use the same block framing.  Testing a second pack type
(Mags has 104 variants across 103 groups — one magazine has two variants — and a
different field layout) confirms that `map_size` and `group_count` decode correctly for
a pack type whose field count differs from Games.

```
TLV:     output/Mags(2025-07-24).tlv
Profile: NULL (default scoring)
Search:  none
Assert:
  rc == WHDTLV_OK
  summary.groups_total == 103
```

---

### Test T15 — Multi-lane profile produces correct lane count and consistent output

**Purpose:** Confirms that the multi-bucket profile binds correctly on Amiga and that
lane selection is driven by correctly decoded token IDs.  `selection_lanes > 1` alone
can pass even when all variants score into the wrong lane — so the test also checks
that lane count matches the host baseline exactly and that selected variant count is
consistent.

`multi_bucket_reference.profile` defines chipset × language buckets (AGA/ECS,OCS × En/De),
which on the host produces four lanes.

```
TLV:     output/Game(2026-04-17).tlv
Profile: assets_raw/profiles/multi_bucket_reference.profile
Search:  none
Assert:
  rc == WHDTLV_OK
  summary.selection_lanes == HOST_MULTI_LANES     /* expected: 4 — confirm from host */
  results.count == summary.selected_variants      /* list length matches summary */
  summary.selected_variants > 0
```

---

## Pass Criteria

The endianness cross-validation is considered **complete** when all 6 logical tests
(comprising 34 individual assertions) pass on an Amiga-target binary.  The 6 tests are
T10, T11, T12 (six fixture sub-cases), T13, T14, and T15.  The 34 assertions are the
sum of per-test checks listed in the Phase 3 and Phase 4 breakdowns.

Run under WinUAE or on physical hardware with AGA, 4 MB Fast RAM, and a raised stack:

```
STACK 100000
run test_amiga_endian
```

Expected output ending:

```
========================================
Results: 34 passed, 0 failed
========================================
```

Exit code 0 = all pass.  Exit code 1 = at least one failure.

---

## Makefile Rules

Add the following target alongside the existing `test-filter` target.  The binary is
built for whatever `TARGET` is active.  On Amiga it is built and transferred to the
device; it cannot be run from the Makefile.

```makefile
BIN_TEST_ENDIAN := $(BUILD_DIR)/test_amiga_endian$(EXE_SUFFIX)

TEST_ENDIAN_OBJ := $(LIB_OBJ) \
                   $(BUILD_DIR)/tests/filtering/test_amiga_endian.o

$(BIN_TEST_ENDIAN): $(TEST_ENDIAN_OBJ)
	$(CC) $(TEST_ENDIAN_OBJ) $(LDFLAGS) -o $@

test-amiga-endian: $(BIN_TEST_ENDIAN)
ifeq ($(TARGET),amiga)
	@echo Amiga endian test binary: $(subst /,\,$(BIN_TEST_ENDIAN)) -- run on device with STACK 100000
else
	$(subst /,\,$(BIN_TEST_ENDIAN))
endif
```

Note: `EXE_SUFFIX` is `.exe` for host and empty for Amiga — define it alongside the
existing `BIN_FH` / `BIN_TC` declarations in the Makefile ifeq blocks.

---

## File Transfer Warning

The CSV files in `assets_raw/defs/` have Windows CRLF line endings.  The CRC validator
normalises `\r\n` to `\n` before hashing, so CRLF files are expected and correct.
**Do not convert line endings** when transferring files to the Amiga, and do not open
or re-save any CSV on the Amiga before running the tests.  If using FTP, set binary
mode for the entire test bundle.  Any line-ending conversion will change the CRC-32 of
the CSV files relative to the values embedded in the TLV, causing T13 to fail with
`crc_mismatches > 0`.

---

## Fixture Files

The T12 fixture text files live in:

```
tests/filtering/expected/endian/
```

Each file contains the exact filenames that the host binary selected for one
(search, profile) pair — one filename per line, no trailing whitespace, no header.
Empty files represent no-match fixtures.

These files are checked into the repository alongside the test source.  They are the
permanent record of what the host produced.  If the TLV files are regenerated or a
profile changes, the fixture files must be regenerated from the host and the embedded
C arrays in `test_amiga_endian.c` updated to match.

Do not edit fixture files by hand.  Always regenerate them by running
`build\host\filter_harness.exe` against the current host binary and TLV files.

**Fixture invalidation:** Any change to the following requires regenerating all fixture
text and summary files and updating the embedded C arrays in `test_amiga_endian.c`:

- The TLV generator (`tlv_builder.c`, `filename_processor.c`, or related pipeline)
- CSV definition files in `assets_raw/defs/`
- Profile files used by any fixture
- `pack_types.ini`
- The fixture TLV files themselves (if rebuilt with `make run`)

Stale fixture data that was generated before one of the above changes is not a valid
oracle.  Treat the host binary and the fixture TLV files as a matched pair.

---

## Implementation Order

### Phase 1 — Capture host baselines (T10, T11, T14, T15) ✓ COMPLETE (2026-05-27)

1. Run `build\host\test_filter_facade.exe` and record from Test 1 output:
   - `HOST_GAME_VARIANTS` (`variants_total`)
   - `HOST_GAME_GROUPS`   (`groups_total`)
   - `HOST_GAME_SELECTED` (`selected_variants`)
2. Run the host binary with `multi_bucket_reference.profile`, no search, and record
   `HOST_MULTI_LANES` (`selection_lanes`).
3. Run the host binary against `Mags(2025-07-24).tlv` with no profile and no search;
   record `HOST_MAGS_GROUPS` (`groups_total`).

#### Captured values

Items 1 and 2 were read directly from `build\host\test_filter_facade.exe` output
(Tests 1 and 4).  Item 3 was obtained by a temporary standalone probe that called
`whdtlv_filter_to_list()` directly; `whdtlv_report.exe` crashes on the Mags TLV
(access violation) and cannot be used for this purpose.

| Symbol               | Value | Source                                          |
|----------------------|-------|-------------------------------------------------|
| `HOST_GAME_VARIANTS` | 3973  | `test_filter_facade.exe` Test 1 `variants=`     |
| `HOST_GAME_GROUPS`   | 2904  | `test_filter_facade.exe` Test 1 `groups=`       |
| `HOST_GAME_SELECTED` | 2904  | `test_filter_facade.exe` Test 1 `selected=`     |
| `HOST_MULTI_LANES`   | 4     | `test_filter_facade.exe` Test 4 `lanes=`        |
| `HOST_MAGS_GROUPS`   | 103   | Standalone probe: `groups_total` for Mags TLV   |

**Important — `HOST_MAGS_GROUPS` is 103, not 104.**  The remediation log entry
"Mags 104 entries" refers to `variants_total` (104 individual variants).  One
magazine has two variants, so `groups_total` is one less: 103.  T14 must assert
`summary.groups_total == 103`.

### Phase 2 — Discover and record T12 fixtures ✓ COMPLETE (2026-05-27)

4. Build `filter_harness.exe` if not already built: `make filter`.
5. Run `build\host\filter_harness.exe` with multiple (search, profile) combinations
   against `output\Game(2026-04-17).tlv` and `assets_raw\defs`.  Examine output files
   to identify 5–8 fixtures that together satisfy all coverage criteria listed in T12
   (chipset, language, memory, multi-lane, no-match, second profile).
6. For each selected fixture, save the output as a text file under
   `tests\filtering\expected\endian\` using the naming convention described in T12.
   Verify the file contains the expected lines by inspection.
7. Create `tests\filtering\expected\endian\` and commit all fixture text files.

#### Implementation notes

`filter_harness.exe` did not exist; it was created at `tools_src/filter_harness/main.c`
with a `make filter` Makefile target.  `whdtlv_report.exe` was not used for diversity
verification — it produced garbled CSV output against the new big-endian TLV at the
time of Phase 2 work.  The cause was not fully investigated during this phase (it may
be a stale build, an unconverted report path, or a separate reporting-layer bug); a
follow-up item is recorded at the end of this document.  Instead, diversity was confirmed by
comparing filter results between profiles: different winners under `pal_aga_4mb` vs
`chipset_legacy_only` prove that the chipset token ID is being decoded and scored
correctly.

A first-letter batch scan (`a*` through `z*`) with `multi_bucket_reference.profile`
confirmed that multi-variant groups exist throughout the dataset.

#### Fixtures selected

Six fixtures satisfying all coverage criteria:

| ID   | Search          | Profile                      | Groups | Selected | Lanes | Coverage                                  |
|------|-----------------|------------------------------|--------|----------|-------|-------------------------------------------|
| F01  | `AlienBreed2*`  | `pal_aga_4mb.profile`        | 1      | 1        | 1     | Chipset token: AGA winner                 |
| F02  | `AlienBreed2*`  | `chipset_legacy_only.profile`| 1      | 1        | 1     | Second profile; chipset winner changes    |
| F03  | `lotus*`        | `pal_aga_4mb.profile`        | 3      | 3        | 1     | Multi-group; token IDs across three groups|
| F04  | `body*`         | `pal_aga_4mb.profile`        | 2      | 2        | 1     | Multi-group chipset diversity             |
| F05  | `zool*`         | `multi_bucket_reference.profile` | 2  | 4        | 4     | Multi-lane: chipset×language buckets      |
| F06  | `zzznomatchzzz*`| `pal_aga_4mb.profile`        | 0      | 0        | 1     | No-match: `matched_groups == 0`           |

F01 vs F02 is the core chipset diversity proof: same TLV, same group, different
profile → different winner.  The change in filename confirms the chipset token ID in
the TLV is decoded correctly by the scoring path.

F05 exercises language token ID decoding via the multi-lane `multi_bucket_reference`
profile: `En/De` buckets form two language lanes.  If language token IDs were
byte-swapped, variants would be mis-classified into the wrong lane.

Memory token coverage is implicit throughout — memory weights are evaluated alongside
chipset and language in every fixture.

All fixture files are saved in `tests/filtering/expected/endian/` (12 files: 6 `.txt`
and 6 `.summary` sidecars).

### Phase 3 — Implement and validate the test source ✓ COMPLETE (2026-05-27)

8. Implement `tests/filtering/test_amiga_endian.c`:
   - T10, T11, T13, T14, T15: use the numeric baseline values from Phase 1.
   - T12: embed the fixture expected arrays from Phase 2 as static C string arrays;
     add one sub-case per fixture.
9. Run `make test-amiga-endian` on host (TARGET=host).  All assertions must pass.
   If any T12 fixture fails on host, the fixture file or C array is wrong — fix it
   before proceeding.

#### Implementation notes

`tests/filtering/test_amiga_endian.c` was created following the same pattern as
`tests/filtering/test_filter_facade.c`: C89-compatible, `whdtlv/whdtlv.h`-only,
no internal headers, all variables declared at block start.

The Makefile was updated with:
- `BIN_TEST_ENDIAN` variable in both `ifeq (TARGET,amiga)` and `else` blocks.
- `TEST_ENDIAN_OBJ` definition (mirrors `TEST_FILTER_OBJ` pattern).
- `test-amiga-endian` target with platform-specific run/message logic.
- `$(BIN_TEST_ENDIAN)` link rule.

The fixture arrays were embedded verbatim from the Phase 2 `.txt` files.
`static const char *arr[]` (without double `const`) was used throughout for
vbcc compatibility as noted in the plan.

#### Host run results (2026-05-27)

Command: `make test-amiga-endian` (TARGET=host, GCC)

```
Results: 34 passed, 0 failed
```

Per-test breakdown:

| Test | Assertions | Result |
|------|-----------|--------|
| T10  | rc==OK, variants_total==3973, groups_total==2904 | PASS (3 checks) |
| T11  | rc==OK, selected_variants==2904, selection_lanes==1 | PASS (3 checks) |
| T12 F01 | rc==OK, count==1, filenames match | PASS (3 checks) |
| T12 F02 | rc==OK, count==1, filenames match | PASS (3 checks) |
| T12 F03 | rc==OK, count==3, filenames match | PASS (3 checks) |
| T12 F04 | rc==OK, count==2, filenames match | PASS (3 checks) |
| T12 F05 | rc==OK, lanes==4, count==4, filenames match | PASS (4 checks) |
| T12 F06 | rc==OK, count==0, matched_groups==0 | PASS (3 checks) |
| T13  | rc==OK, crc_mismatches==0, crc_files_checked==18 | PASS (3 checks) |
| T14  | rc==OK, groups_total==103 | PASS (2 checks) |
| T15  | rc==OK, lanes==4, count==selected_variants, selected>0 | PASS (4 checks) |

All 34 assertions passed.  No fixture mismatches.  No CRC failures.  The host
build is now the reference baseline for the Amiga device run in Phase 4.

### Phase 4 — Amiga build and device run ✓ COMPLETE (2026-05-27)

10. Run `make TARGET=amiga test-amiga-endian` to build the Amiga binary.
11. Assemble and transfer the self-contained test bundle using `assemble_bundle.bat`
    (created 2026-05-27).  The script uses binary-mode `xcopy` to produce
    `amiga_bundle/` containing:
    - `test_amiga_endian` (Amiga binary)
    - `output/Game(2026-04-17).tlv`
    - `output/Mags(2025-07-24).tlv`
    - `assets_raw/defs/` (all CSVs, CRLF preserved)
    - `assets_raw/profiles/pal_aga_4mb.profile`
    - `assets_raw/profiles/chipset_legacy_only.profile`
    - `assets_raw/profiles/multi_bucket_reference.profile`
12. Mount `amiga_bundle/` in WinUAE as a read-write directory hard drive (DH1:).
    Run from the Amiga CLI:
    ```
    STACK 100000
    cd DH1:
    test_amiga_endian >output/amiga_results.txt
    ```
13. Results captured at `amiga_bundle/output/amiga_results.txt` and reviewed from
    the host via the shared folder.

#### Bug found and fixed during Phase 4

The first Amiga run produced a HALT3 (address error) immediately at the start of T10,
before any assertion output appeared.  HALT3 on a 68000 is a CPU exception caused by
a word or longword read/write to an odd address — in practice here, a stack overflow
corrupting a return address to an odd value, with the subsequent `RTS` triggering the
error.

The root cause was two large structs declared as local variables in
`whdtlv_filter_to_list()` in `src/whdtlv/whdtlv_filter_facade.c`:

| Local | Size | Detail |
|---|---|---|
| `TlvRuntime rt` | ~8,600 bytes | `TlvFieldEntry[252]` at 33 bytes each inside `TlvFieldMap` |
| `WhdBoundProfile profile` | ~7,400 bytes | `rank_by_id[256]` in each of 16 `WhdBoundField` entries |

Combined stack frame: ~16 KB on the first call, which exceeds the default AmigaDOS
Shell stack (4,000–8,000 bytes) instantly, even with `STACK 100000` protecting the
top-level test function.  The overflow occurs inside the facade call itself.

**Fix:** both locals converted to heap-allocated pointers (`malloc`/`free`) with
all usage and all error-path cleanups updated accordingly.  Returns
`WHDTLV_FILTER_ERR_ALLOC` (-7) if either allocation fails.  The stack frame for
`whdtlv_filter_to_list()` is now ~100 bytes regardless of TLV content.

Host test suite re-run after the fix: 34 passed, 0 failed.  Amiga binary rebuilt.

#### Amiga device run results (2026-05-27)

Emulator: WinUAE with `amiga_bundle/` mounted as DH1: (read-write directory hard drive).
Output redirected to `amiga_bundle/output/amiga_results.txt`.

```
Results: 34 passed, 0 failed
```

Per-test breakdown:

| Test | Assertions | Result |
|------|-----------|--------|
| T10  | rc==OK, variants_total==3973, groups_total==2904 | PASS (3 checks) |
| T11  | rc==OK, selected_variants==2904, selection_lanes==1 | PASS (3 checks) |
| T12 F01 | rc==OK, count==1, filename==AlienBreed2_v1.6_AGA_0044 | PASS (3 checks) |
| T12 F02 | rc==OK, count==1, filename==AlienBreed2_v1.6_0278 | PASS (3 checks) |
| T12 F03 | rc==OK, count==3, filenames match (Lotus2, Lotus3, Lotus) | PASS (3 checks) |
| T12 F04 | rc==OK, count==2, filenames match (BodyBlows, BodyBlowsGalactic) | PASS (3 checks) |
| T12 F05 | rc==OK, lanes==4, count==4, filenames match (Zool2 AGA, Zool2, Zool AGA, Zool) | PASS (4 checks) |
| T12 F06 | rc==OK, count==0, matched_groups==0 | PASS (3 checks) |
| T13  | rc==OK, crc_mismatches==0, crc_files_checked==18 | PASS (3 checks) |
| T14  | rc==OK, groups_total==103 | PASS (2 checks) |
| T15  | rc==OK, lanes==4, count==selected_variants, selected>0 | PASS (4 checks) |

All 34 assertions passed.  Fixture parity confirmed between host oracle and Amiga
output for all six T12 cases, including the chipset diversity pair (F01 AGA winner vs
F02 OCS winner), the multi-lane case (F05 four lanes), and the no-match case (F06).
CRC validation passed with 18 files checked and zero mismatches.

The endianness remediation (Items 1–12) is fully validated end-to-end.

---

## Relation to Existing Tests

`tests/filtering/test_filter_facade.c` (Tests 1–9) remains unchanged.  The new file is
additive and independent.  Both target `tests/filtering/` and both use only
`whdtlv/whdtlv.h`.  They build as separate binaries and can be run independently.

---

## Scope of Validation Closed by This Document

This plan closes the following scope:

**Host-built BE TLV + copied defs/profiles + Amiga filtering facade → same selected
output as the host oracle.**

Evidence:
- Host run: 34 passed, 0 failed (GCC x64, 2026-05-27).
- Amiga run under WinUAE: 34 passed, 0 failed (vbcc 68000, 2026-05-27).
- Exact fixture parity for all six T12 cases.
- CRC validation passed (18 files, 0 mismatches).
- Stack-frame crash (HALT3) found, fixed, and retested.

What is **not** closed by this document:

| Item | Status |
|---|---|
| Physical Amiga hardware confirmation | Optional Phase 5 — not yet run |
| `whdtlv_report.exe` garbled CSV output against BE TLV | Follow-up required — cause unknown |
| Report layer endianness audit | Separate work item; not covered by the filter facade tests |

### Phase 5 — Physical Amiga hardware confirmation (optional)

The WinUAE run is a high-confidence Amiga-target validation: it uses the vbcc 68000
binary and caught a real 68k-specific crash (HALT3 from stack overflow).  Physical
hardware would additionally exercise real Amiga memory timing, custom chip interaction,
and real AmigaDOS stack behaviour.

If physical hardware confirmation is desired, repeat the Phase 4 procedure with the
same `amiga_bundle/` on real hardware.  Expected result: identical 34/0 output.

### Follow-up: `whdtlv_report.exe` garbled CSV against BE TLV

During Phase 2, `whdtlv_report.exe` produced garbled CSV output when run against the
new big-endian TLV files.  This was noted but not investigated further because the
filter facade tests do not depend on the report tool.  Before declaring all host tooling
finished, this should be investigated:

1. Confirm whether `build\host\whdtlv_report.exe` was rebuilt after the endianness
   remediation (`make report` or equivalent), or whether a stale pre-remediation binary
   was still present.
2. If the binary is current, identify which report-layer read path is not yet using
   `tlv_read_u32_be()` / `tlv_read_u16_be()`.
3. Fix, rebuild, and verify that the report CSV output for
   `output\Game(2026-04-17).tlv` is no longer garbled.
