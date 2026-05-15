# Handover: Profile-Aware Selection Trace Reporting
**Date:** 2026-05-15  
**Branch:** main  
**Status:** Tasks A and B complete; Task C (tests + docs) not started.

---

## 1. Feature Overview

The goal is a `--mode profile` in `whdtlv_report` that runs the real filter selector against an existing `.tlv` and `.profile`, then writes a CSV showing every variant's selection decision: winner, loser, rejected, lane-ineligible, duplicate-suppressed. The implementation traces the real selector — no second copy of filter logic.

The original requirements prompt is at [docs/AI-Sessions/whdtlv_profile_report_trace_prompt.md](whdtlv_profile_report_trace_prompt.md).

---

## 2. Task Plan

### Task A — Compile-Guarded Trace Types + Selector Hooks
**Status: COMPLETE**

Add trace enums, structs, and collector behind `WHDTLV_ENABLE_SELECTION_TRACE`, hook the real selector.

### Task B — Profile Report Writer + CLI Extension + Makefile
**Status: COMPLETE**

Add the CSV writer module, extend `whdtlv_report` with `--mode profile`, wire the Makefile.

### Task C — Tests + Documentation
**Status: NOT STARTED**

Write host-side tests and update `docs/reporting-tool.md`. See section 7 for full test spec.

---

## 3. Task A — What Was Done

### New files

**`src/whdtlv/filtering/tlv_select_trace.h`**  
Compile-guarded (`#if WHDTLV_ENABLE_SELECTION_TRACE`) trace types:

```c
typedef enum WhdTlvTraceReason {
    WHDTLV_TRACE_REASON_WINNER = 0,
    WHDTLV_TRACE_REASON_LOST_SCORE,
    WHDTLV_TRACE_REASON_REJECTED_EXCLUDE,
    WHDTLV_TRACE_REASON_NOT_LANE_ELIGIBLE,
    WHDTLV_TRACE_REASON_DUPLICATE_SUPPRESSED,
    WHDTLV_TRACE_REASON_SEARCH_GROUP_SKIPPED,
    WHDTLV_TRACE_REASON_NO_SCORE,
    WHDTLV_TRACE_REASON_UNKNOWN
} WhdTlvTraceReason;

typedef struct WhdTlvSelectionTraceRow {
    unsigned long  group_index;
    unsigned long  variant_index;         /* into WhdVariantArray.items[]     */
    unsigned long  lane_index;            /* 0xFFFFFFFFul = not lane-specific  */
    unsigned short group_id;

    int selected;           /* 1 = winner */
    int rejected;           /* 1 = excluded by profile */
    int eligible;           /* 1 = passed lane requirements */
    int duplicate_suppressed;

    unsigned long score_total;
    unsigned long lost_to_score;
    unsigned long lost_to_variant_index;  /* 0xFFFFFFFFul if N/A */

    unsigned char reject_field_index;     /* profile field index; 0xFF = N/A */

    WhdTlvTraceReason reason;
} WhdTlvSelectionTraceRow;

typedef struct WhdTlvSelectionTrace {
    WhdTlvSelectionTraceRow *rows;
    unsigned long            count;
    unsigned long            capacity;
} WhdTlvSelectionTrace;
```

Public API: `whdtlv_trace_init()`, `whdtlv_trace_free()`, `whdtlv_trace_add_row()`.

**`src/whdtlv/filtering/tlv_select_trace.c`**  
Growable collector. Initial capacity 256, doubles via `realloc`. Entire file guarded by `#if WHDTLV_ENABLE_SELECTION_TRACE`. C89-compatible.

### Modified files

**`src/whdtlv/filtering/tlv_select.h`**  
Added at end of public API section (guarded):

```c
#if WHDTLV_ENABLE_SELECTION_TRACE
#include "whdtlv/filtering/tlv_select_trace.h"
int tlv_select_run_traced(WhdSelectResult *out,
                           const WhdGroupSet *gs,
                           const WhdVariantArray *arr,
                           const WhdBoundProfile *profile,
                           const WhdGroupAllowList *allow,
                           WhdTlvSelectionTrace *trace);
#endif
```

**`src/whdtlv/filtering/tlv_select.c`**  
Key changes:

1. `tlv_select_run` body moved to `static tlv_select_run_impl`. Conditional signature: with `WhdTlvSelectionTrace *trace` when trace enabled, without it otherwise.
2. Five trace hooks inside `tlv_select_run_impl`:
   - **Pre-pass rejection**: emits `REJECTED_EXCLUDE` row with `reject_field_index = vs.reject_field`.
   - **Duplicate suppression**: emits `DUPLICATE_SUPPRESSED` row.
   - **Lane eligibility failure**: emits `NOT_LANE_ELIGIBLE` row.
   - **Eligible variant accumulation**: fixed-size stack (`TRACE_ELIG_MAX = 128`) collects eligible variants with scores inside each lane loop.
   - **Winner/loser emission**: after each lane's winner is found, emits `WINNER`, `LOST_SCORE`, or `NO_SCORE` rows for all accumulated variants.
3. Public `tlv_select_run()` wraps `tlv_select_run_impl(..., NULL)`.
4. `tlv_select_run_traced()` wraps `tlv_select_run_impl(..., trace)`.

Selection results are **identical** to `tlv_select_run()` for the same inputs — no behaviour change.

---

## 4. Task B — What Was Done

### New files

**`src/whdtlv/reporting/whdtlv_report_profile.h`**  
Host-only header (`#ifdef PLATFORM_AMIGA #error`). Defines:

```c
#define WHDTLV_PROFILE_REPORT_OK            0
#define WHDTLV_PROFILE_REPORT_ERR_BAD_ARG  (-1)
#define WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN (-2)
#define WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE(-3)
#define WHDTLV_PROFILE_REPORT_ERR_CSV_OPEN (-4)
#define WHDTLV_PROFILE_REPORT_ERR_OOM      (-5)
#define WHDTLV_PROFILE_REPORT_ERR_PROFILE  (-6)

typedef struct WhdTlvProfileReportOptions {
    const char *tlv_path;
    const char *defs_dir;
    const char *profile_path;
    const char *search_pattern;   /* NULL or "" = all groups */
    const char *output_csv_path;
} WhdTlvProfileReportOptions;

typedef struct WhdTlvProfileReportSummary {
    unsigned long groups_total;
    unsigned long variants_total;
    unsigned long rows_written;
    unsigned long winners;
    unsigned long losers;
    unsigned long rejected;
    unsigned long not_eligible;
    unsigned long dup_suppressed;
} WhdTlvProfileReportSummary;

int whdtlv_report_profile_file(
    const WhdTlvProfileReportOptions *opts,
    WhdTlvProfileReportSummary       *summary);
```

**`src/whdtlv/reporting/whdtlv_report_profile.c`**  
Full implementation. Pipeline inside `whdtlv_report_profile_file()`:

1. `tlv_runtime_init` / `tlv_runtime_load` — load TLV.
2. Resolve `display_fid` and `group_id_fid`.
3. `tlv_variant_build` — build variant array.
4. `csv_cache_manager_init` — init CSV manager.
5. `tlv_group_build` — build group set.
6. `whd_profile_load` — load and bind `.profile`.
7. `whd_build_selection_plan` — generate lane plan.
8. `whd_search_build_group_allow_list` (if `search_pattern` provided).
9. `whdtlv_trace_init` then `tlv_select_run_traced`.
10. `build_field_info` — pre-load per-profile-field CSVs for token resolution.
11. Open output CSV, write header row, walk `trace.rows[]`, call `write_trace_row()` per row.

Private `ProfCtx` struct owns TLV state (does not share the private `ReptCtx` from `whdtlv_report_csv.c`).

**CSV column schema:**

```
selected_marker, selected_rank, reason_code, selection_lane, lane_requirements,
group_id, group_name, display_name, score_total, reject_field,
lost_to_display_name, lost_to_score,
<field>, <field>_effective, <field>_effective_source  [repeated per profile field]
```

Marker values: `X` winner, `-` lost/no-score, `R` rejected, `N` not-lane-eligible, `D` dup-suppressed, `S` search-skipped, `?` unknown.

Per-field effective source values: `explicit`, `default`, `missing`.

Lane requirements example: `chipset=AGA; language=En` or `single-lane` for no-bucket lanes.

### Modified files

**`tools_src/whdtlv_report/main.c`**  
Added:
- `#include "whdtlv/reporting/whdtlv_report_profile.h"`.
- Variables `profile_path`, `search_pattern`, `mode_is_profile`.
- `--profile <path>` and `--search <pattern>` argument parsing.
- `--mode profile` branch in mode parsing.
- After argument validation: if `mode_is_profile`, validates `--profile` is set, builds `WhdTlvProfileReportOptions`, calls `whdtlv_report_profile_file()`, prints profile summary table, returns.
- Updated `print_usage()` to show new options.
- Error message maps `WHDTLV_PROFILE_REPORT_ERR_PROFILE` → exit code 6.

**`Makefile`**  
Key changes:

```make
# Extra flags used only for trace-enabled report binary
TRACE_CFLAGS := $(CFLAGS) -DWHDTLV_ENABLE_SELECTION_TRACE=1

# Trace-enabled object names (compiled separately; never pollute Amiga build)
TRACE_SELECT_OBJ  := $(BUILD_DIR)/src/whdtlv/filtering/tlv_select_tr.o
TRACE_COLLECT_OBJ := $(BUILD_DIR)/src/whdtlv/filtering/tlv_select_trace.o
PROF_REPORT_OBJ   := $(BUILD_DIR)/src/whdtlv/reporting/whdtlv_report_profile.o

# LIB_OBJ minus tlv_select.o (replaced by trace-enabled variant in report binary)
REPORT_LIB_OBJ := $(filter-out $(BUILD_DIR)/src/whdtlv/filtering/tlv_select.o,$(LIB_OBJ))

REPORT_TOOL_OBJ := $(REPORT_LIB_OBJ) \
                   $(TRACE_SELECT_OBJ) \
                   $(TRACE_COLLECT_OBJ) \
                   $(REPORT_OBJ) \
                   $(PROF_REPORT_OBJ) \
                   $(BUILD_DIR)/tools_src/whdtlv_report/main.o
```

Explicit rules compile `tlv_select_tr.o`, `tlv_select_trace.o`, and `whdtlv_report_profile.o` with `TRACE_CFLAGS`. The normal `tlv_select.o` in `LIB_OBJ` (without the trace flag) is still used by the main binary, the Amiga binary, `test-filter`, `test-report`, `test-effective`, and `test-language`.

---

## 5. Verified Build State

After Tasks A and B, all targets build cleanly on `TARGET=host`:

```
make TARGET=host            ✓  (dat_to_tlv.exe; no trace)
make TARGET=host report     ✓  (whdtlv_report.exe; trace enabled)
make TARGET=host test-filter  ✓  38 passed, 0 failed
make TARGET=host test-report  ✓  56 passed, 0 failed
make TARGET=host test-effective ✓  34 passed, 0 failed
```

Smoke-test run (2026-05-15):

```
whdtlv_report --tlv output\Game(2026-04-17).tlv
              --defs assets_raw\defs
              --profile assets_raw\profiles\pal_aga_4mb.profile
              --out output\game_profile_report.csv
              --mode profile

Groups scanned  : 2904
Variants scanned: 3973
Rows written    : 3973
Winners         : 2904
Losers          : 1069
Rejected        : 0
Not eligible    : 0
Dup-suppressed  : 0
```

Winners == Groups (2904): correct for a single-lane profile where every group has at least one non-rejected variant.

Search smoke-test (`--search "lotus*"`): 5 rows, 3 winners, 2 losers — matches the filter test result from test-filter Test 2 (matched_groups=3).

---

## 6. Files Changed (Task A + B)

| File | Status |
|---|---|
| `src/whdtlv/filtering/tlv_select_trace.h` | New |
| `src/whdtlv/filtering/tlv_select_trace.c` | New |
| `src/whdtlv/filtering/tlv_select.h` | Modified |
| `src/whdtlv/filtering/tlv_select.c` | Modified |
| `src/whdtlv/reporting/whdtlv_report_profile.h` | New |
| `src/whdtlv/reporting/whdtlv_report_profile.c` | New |
| `tools_src/whdtlv_report/main.c` | Modified |
| `Makefile` | Modified |

---

## 7. Task C — What Remains

### 7.1 Tests

Create `tests/reporting/test_profile_report.c`. Register in `Makefile` as a new `test-profile` target using the `TRACE_CFLAGS` pattern (same as `REPORT_TOOL_OBJ`).

**Minimum test cases** (from original prompt):

1. **Filter parity** — Run `whdtlv_filter_to_file()` to produce `selected.txt`. Run profile report. Extract rows where `selected_marker=X`. Confirm filenames match `selected.txt` exactly. This is the most important test.
2. **One winner per group** — Single-lane profile: for each `group_id` in the CSV, confirm exactly one row has `selected_marker=X`.
3. **Loser has `reason_code=lost_score`** — For groups with multiple non-rejected variants, confirm at least one `-` row with `reason_code=lost_score` and `lost_to_display_name` populated.
4. **Rejected variant** — Use a profile that excludes `CD32` chipset. Confirm at least one `R` row with `reason_code=rejected_exclude` and `reject_field=chipset`.
5. **Multi-lane profile** — Use the `multi_bucket_reference.profile` fixture (already used in test-filter Test 4). Confirm `selection_lane` column is populated and more than one distinct lane value appears.
6. **Multi-lane multi-winner** — For a group where two lanes each select a different variant, confirm two `X` rows in the same `group_id`.
7. **Lane requirements string** — Multi-lane profile: confirm `lane_requirements` is non-empty and contains the field name(s) used in slash buckets.
8. **Effective default** — Missing `language` field: confirm `language_effective` = `en`, `language_effective_source` = `default`.
9. **Effective explicit** — Variant with explicit `language=de`: confirm `language_effective` = `de`, `language_effective_source` = `explicit`.
10. **Missing field, no CSV default** — Variant missing a field that has no CSV default: confirm `<field>_effective_source` = `missing`.
11. **Search narrowing** — Pattern `lotus*`: confirm `rows_written` == `variants_total_for_matched_groups`. Confirm no `X` rows appear for groups not matching the pattern.
12. **Summary counters** — Confirm `summary.winners + summary.losers + summary.rejected + summary.not_eligible + summary.dup_suppressed == summary.rows_written`.
13. **CSV escape** — Confirm a group name or display name containing a comma or double-quote is properly double-quoted in the output file.
14. **Missing profile path** — `--mode profile` without `--profile` must print an error to stderr and return non-zero.
15. **Regression: existing report modes unchanged** — Run `--mode wide` before and after this change. Confirm output is byte-identical.
16. **Normal build unaffected** — Confirm `dat_to_tlv.exe` still builds without `WHDTLV_ENABLE_SELECTION_TRACE` and produces correct TLV output.

**Suggested test harness structure** (follow the pattern in `tests/reporting/test_report_csv.c`):

```c
static const char *TLV_PATH   = "output/Game(2026-04-17).tlv";
static const char *DEFS_PATH  = "assets_raw/defs";
static const char *PROF_PATH  = "assets_raw/profiles/pal_aga_4mb.profile";
static const char *OUT_PATH   = "output/test_profile_report.csv";

/* Test 1: filter parity */
static void test_filter_parity(int *passed, int *failed) {
    /* run whdtlv_filter_to_file -> collect to a set -> run profile report
     * -> extract X rows -> compare filenames */
}
```

Use `assets_raw/profiles/chipset_aga_only.profile` (or create a small fixture profile that excludes at least one token) to generate `R` rows for test 4.

### 7.2 Makefile addition for test-profile target

```make
BIN_TEST_PROFILE := $(BUILD_DIR)/test_profile_report.exe

TEST_PROFILE_OBJ := $(REPORT_LIB_OBJ) \
                    $(TRACE_SELECT_OBJ) \
                    $(TRACE_COLLECT_OBJ) \
                    $(REPORT_OBJ) \
                    $(PROF_REPORT_OBJ) \
                    $(BUILD_DIR)/tests/reporting/test_profile_report.o

test-profile: $(BIN_TEST_PROFILE)
    $(subst /,\,$(BIN_TEST_PROFILE))

$(BIN_TEST_PROFILE): $(TEST_PROFILE_OBJ)
    @$(call MKDIR_CMD,$(BUILD_DIR))
    $(CC) $(TRACE_CFLAGS) -o $@ $(TEST_PROFILE_OBJ) $(LDFLAGS)

$(BUILD_DIR)/tests/reporting/test_profile_report.o: tests/reporting/test_profile_report.c
    @$(call MKDIR_CMD,$(dir $@))
    $(CC) $(TRACE_CFLAGS) -c $< -o $@
```

Add `test-profile` to the `ifneq ($(TARGET),amiga)` block alongside `test-report` and `test-effective`.

### 7.3 Documentation

Update `docs/reporting-tool.md` with a new section covering:

- `--mode profile` — what it does, when to use it.
- `--profile <path>` — required for profile mode.
- `--search <pattern>` — optional, narrows to matched groups.
- Marker value legend (`X`, `-`, `R`, `N`, `D`, `S`).
- Column descriptions for `selection_lane`, `lane_requirements`, `reject_field`, `lost_to_display_name`.
- Effective source values (`explicit`, `default`, `missing`).
- Note that profile mode always includes effective columns for all profile-bound fields.
- Note that trace code is host/reporting-only (excluded from Amiga builds).
- Example command lines.
- Example CSV snippet (copy from the original prompt's acceptance criteria section or generate from smoke-test output).

---

## 8. Known Gaps / Future Work

- `reject_token` column: the prompt requested it, but the trace currently stores only `reject_field_index`. The reject token ID could be recovered from the variant's field values post-hoc in `write_trace_row()` using the same field scan used for explicit token collection. Not blocking for v1.
- `search_pattern` and `search_matched` columns: the prompt suggested these. Currently absent; the search pattern is not written into the CSV. Easy to add to the fixed columns in `write_header()` / `write_trace_row()`.
- `WHDTLV_TRACE_REASON_SEARCH_GROUP_SKIPPED`: the reason code exists in the enum but the selector does not emit it for skipped groups (it simply does not emit any rows for those groups). If per-skipped-group rows are wanted, the hook must be added to `tlv_select_run_impl` at the `!whd_group_allowed(allow, gi)` branch.
- Per-field score tracing (`WhdTlvTraceFieldScore`): described in the prompt as optional. Not implemented. The total score is stored; individual field contributions are not.
- Compact `--profile-report group` mode (one row per variant rather than one per variant per lane): not needed yet — the current output is already one row per variant for single-lane profiles. For multi-lane profiles this would collapse lanes. Defer until requested.

---

## 9. Resumption Instructions for Next Agent

1. Read this file and [docs/AI-Sessions/whdtlv_profile_report_trace_prompt.md](whdtlv_profile_report_trace_prompt.md) for full requirements.
2. Read [AGENTS.md](../../AGENTS.md) and [.github/instructions/dat-to-tlv-codebase.instructions.md](../../.github/instructions/dat-to-tlv-codebase.instructions.md).
3. Run `make TARGET=host test-filter test-report test-effective` to confirm the baseline (should be 128 tests passing: 38 + 56 + 34).
4. Implement Task C:
   - Create `tests/reporting/test_profile_report.c` (see section 7.1 for test list).
   - Update `Makefile` with the `test-profile` target (see section 7.2 for the exact make rules).
   - Update `docs/reporting-tool.md` (see section 7.3 for documentation spec).
5. Run `make TARGET=host test-profile` and confirm all new tests pass.
6. Run `make TARGET=host test-filter test-report test-effective` again to confirm no regressions.
7. Run `make TARGET=amiga` to confirm the Amiga binary still builds without errors (no `WHDTLV_ENABLE_SELECTION_TRACE` in Amiga CFLAGS).
