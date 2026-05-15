# AI Agent Prompt: Add Profile-Aware Selection Trace Reporting to `whdtlv_report`

## Project Context

You are working in `variant_backport_staging`, the standalone DAT-to-TLV and TLV filtering pipeline for Amiga WHDLoad archive metadata.

The project currently has:

- A TLV builder that converts RetroPlay/WHDLoad DAT entries into a compact binary TLV file.
- A runtime filtering subsystem that reads a prebuilt TLV, binds a `.profile`, groups variants by `group_id`, scores variants, and selects the best archive filename or filenames per game group.
- A host-side reporting tool under the new `src/whdtlv/reporting/` area, with a `tools_src/whdtlv_report/` command-line frontend.
- Existing report modes that export decoded TLV contents in wide and long CSV formats.
- A planned or recently added `--include-effective` option that can show effective CSV-default values beside raw explicit TLV values.

The new feature requested here is a **profile-aware report** that helps users understand why a given profile selected a particular variant and why other variants in the same group did not win.

This must be implemented by tracing the real filtering engine, not by creating a second copy of the selection logic inside the reporting code.

---

## High-Level Goal

Add an optional host/reporting-only selection trace feature that allows `whdtlv_report` to run a real profile against an existing TLV file and export a CSV showing, per group and per variant:

- Which archive was selected by the profile.
- Which archive lost.
- Which archive was rejected by an exclude rule.
- Which archive was not eligible for a selection lane.
- Which archive was duplicate-suppressed in multi-lane selection.
- The score used by the real filter.
- The lane that selected or evaluated the variant.
- The lane requirements, where relevant.
- Effective field values such as language, chipset, memory, disks, etc.
- Optional per-field score/reason information where practical.

The report is intended for profile tuning. A user should be able to change a `.profile`, rerun the report against the same `.tlv`, and see why a different variant was or was not selected.

---

## Critical Design Requirement

Do **not** implement a separate selector in the reporting subsystem.

The profile-aware report must use the actual filtering/scoring code path. The reporting feature should observe or trace what the selector really does.

Reason: the filtering system already has nuanced behaviour, including:

- CSV default token handling.
- Exclude-before-include behaviour.
- Weighted field scoring.
- Multi-value fields such as language.
- Slash-bucket selection lanes.
- Cartesian product lane generation for multiple slash fields.
- Bucket-local ranking inside slash lanes.
- Duplicate suppression across lanes within a group.
- Group-level search pre-filtering.
- First-encountered tie handling.

A second reporting-only implementation will drift as the real filter evolves.

---

## Build/Footprint Requirement

The selection trace must be optional and excluded from the normal WHDFetch/Amiga-facing build.

Use compile guards such as:

```c
#ifdef WHDTLV_ENABLE_SELECTION_TRACE
    /* trace-only code */
#endif
```

or:

```c
#if WHDTLV_ENABLE_SELECTION_TRACE
    /* trace-only code */
#endif
```

The host reporting tool may enable this with something like:

```make
CFLAGS += -DWHDTLV_ENABLE_SELECTION_TRACE=1
```

The normal embedded/WHDFetch build must not allocate trace structures, emit trace rows, carry reason strings, or expose extra public API surface.

---

## Suggested Module Layout

Keep the layering clean:

```text
src/whdtlv/filtering/
    real filter and selector
    optional trace hooks and trace structs behind WHDTLV_ENABLE_SELECTION_TRACE

src/whdtlv/reporting/
    CSV report generation
    profile-aware report writer
    conversion of trace records into CSV rows

tools_src/whdtlv_report/
    command-line frontend
```

Do not add this feature to the public WHDFetch facade unless absolutely necessary.

The public facade should remain focused on:

```c
#include "whdtlv/whdtlv.h"

whdtlv_filter_to_list(...)
whdtlv_filter_to_file(...)
```

If an internal reporting API is required, keep it under `src/whdtlv/reporting/` or behind a host-only/internal header.

---

## Proposed Command-Line Interface

Extend `whdtlv_report` with a profile-aware report mode.

Possible command examples:

```text
whdtlv_report ^
  --tlv output\Games.tlv ^
  --defs assets_raw\defs ^
  --profile assets_raw\profiles\pal_aga_4mb.profile ^
  --out output\Games_profile_report.csv ^
  --mode profile
```

Optional search support:

```text
whdtlv_report ^
  --tlv output\Games.tlv ^
  --defs assets_raw\defs ^
  --profile assets_raw\profiles\pal_aga_4mb.profile ^
  --search lotus* ^
  --out output\Games_lotus_profile_report.csv ^
  --mode profile
```

If the existing tool already uses `--mode wide|long`, add one of these approaches:

Option A:

```text
--mode profile
```

Option B:

```text
--profile-report group
--profile-report lane
```

Option C:

```text
--mode profile-group
--mode profile-lane
```

Recommended first implementation:

```text
--mode profile
```

where the output is one row per variant per relevant lane, or one row per variant if the profile has only one lane. If that becomes too verbose, add `--profile-report group|lane` afterwards.

---

## CSV Output Design

CSV cannot reliably carry colour, so use clear marker and reason columns that are easy to sort and filter in Excel.

Recommended leading columns:

```csv
selected_marker,selected_rank,reason_code,selection_lane,lane_requirements,group_id,group_name,display_name,score_total
```

Suggested marker values:

```text
X = selected winner
- = considered but not selected
R = rejected by exclude rule
N = not eligible for this lane
D = duplicate-suppressed because already selected by an earlier lane
S = skipped because group did not match search
```

Suggested numeric `selected_rank` values:

```text
1 = winner
0 = not winner
```

This allows Excel sorting with winners first.

Suggested reason codes:

```text
winner
lost_score
rejected_exclude
not_lane_eligible
duplicate_suppressed
search_group_skipped
missing_required_field
no_score
```

Use stable short strings rather than long prose. Add separate detail columns for the precise field/token where useful.

Recommended detail columns:

```csv
reject_field,reject_token,lost_to_display_name,lost_to_score,search_pattern,search_matched
```

Recommended effective metadata columns after the selection columns:

```csv
chipset,chipset_effective,chipset_effective_source,chipset_score
language,language_effective,language_effective_source,language_score
memory,memory_effective,memory_effective_source,memory_score
disks,disks_effective,disks_effective_source,disks_score
```

The report should still be schema-driven where possible. Do not hard-code only these fields if the existing reporting system can iterate the TLV field map and CSV-backed fields.

---

## Lane Requirements

For normal comma-only profiles, use:

```text
single-lane
```

or leave the lane requirements column blank.

For slash-bucket profiles, emit a readable lane requirement string.

Example profile:

```ini
[Filter.chipset]
include=AGA/ECS,OCS

[Filter.language]
include=En/De
```

Possible lane requirement strings:

```text
chipset=AGA; language=En
chipset=AGA; language=De
chipset=ECS,OCS; language=En
chipset=ECS,OCS; language=De
```

The lane requirements must reflect the actual bound selection plan generated by the real profile binder, not a separately parsed approximation.

---

## Trace Data to Capture

Add a compact trace model behind `WHDTLV_ENABLE_SELECTION_TRACE`.

Minimum useful trace fields:

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
```

Suggested record shape:

```c
typedef struct WhdTlvSelectionTraceRow {
    unsigned int group_index;
    unsigned int variant_index;
    unsigned int original_index;
    unsigned int lane_index;

    unsigned short group_id;

    int selected;
    int rejected;
    int eligible;
    int duplicate_suppressed;

    long score_total;
    long lost_to_score;

    unsigned int lost_to_variant_index;

    unsigned char reject_field_id;
    unsigned int reject_token_id;

    WhdTlvTraceReason reason;
} WhdTlvSelectionTraceRow;
```

If the selector already uses other integer sizes, adapt to match project conventions.

Do not store large strings in each trace row unless necessary. Prefer storing indices and IDs, then let the reporting layer resolve names from existing variant/group/field structures.

Optional per-field scoring trace:

```c
#define WHDTLV_TRACE_MAX_FIELD_SCORES 16

typedef struct WhdTlvTraceFieldScore {
    unsigned char field_id;
    unsigned int effective_token_id;
    int effective_source;      /* explicit, default, missing */
    long score;
    int rank;
    int included;
    int excluded;
} WhdTlvTraceFieldScore;
```

Only add per-field score tracing if it can be done without destabilising the selector. A first version with total score and reason code is acceptable.

---

## Trace Collector API

Create a small optional collector interface. Keep it internal unless a public-facing need is proven.

Example shape:

```c
#ifdef WHDTLV_ENABLE_SELECTION_TRACE

typedef struct WhdTlvSelectionTrace WhdTlvSelectionTrace;

void whdtlv_trace_init(WhdTlvSelectionTrace *trace);
void whdtlv_trace_free(WhdTlvSelectionTrace *trace);

int whdtlv_trace_add_row(
    WhdTlvSelectionTrace *trace,
    const WhdTlvSelectionTraceRow *row
);

#endif
```

Or use a callback if that fits the existing code better:

```c
#ifdef WHDTLV_ENABLE_SELECTION_TRACE

typedef int (*WhdTlvSelectionTraceCallback)(
    void *user,
    const WhdTlvSelectionTraceRow *row
);

#endif
```

A callback can avoid storing everything in memory, but a collector is easier for CSV generation if group/variant names are resolved after selection.

For the first implementation, prefer the simpler approach that matches the existing code style.

---

## Where to Hook the Selector

Add trace hooks at the real decision points:

1. After global reject/pre-pass scoring:
   - Record variants rejected by exclude rules.
   - Capture `reject_field_id` and `reject_token_id` if available.

2. During per-lane eligibility:
   - Record variants that fail lane eligibility as `not_lane_eligible` if lane report mode is active.

3. During duplicate suppression:
   - Record variants skipped because they were already selected for an earlier lane.

4. After lane winner is chosen:
   - Record the selected winner as `winner` with marker `X`.
   - Record eligible non-winning variants as `lost_score`, ideally with `lost_to_variant_index` and `lost_to_score`.

5. During search pre-filter, if search is active:
   - Groups skipped by search do not necessarily need one row per variant by default, because that may bloat the report.
   - Add support only if it is straightforward or make it optional later.

Keep the first version useful but not enormous. It is acceptable for v1 to report only groups that passed search.

---

## Group-Level vs Lane-Level Output

There are two useful forms:

### Group Report

One row per variant. This is compact and easy to read.

For single-lane profiles, this is sufficient.

For multi-lane profiles, it may be ambiguous because the same variant can be evaluated differently per lane.

### Lane Report

One row per variant per lane. This is more verbose but explains slash-bucket behaviour properly.

Recommended staged approach:

1. Implement `--mode profile` as lane-aware output internally.
2. For single-lane profiles, this naturally produces one row per variant.
3. For multi-lane profiles, include `selection_lane` and `lane_requirements` so each lane can be understood.
4. Later, add a compact `--profile-report group` if the lane report is too large.

---

## Interaction With Existing `--include-effective`

Profile-aware reports should show effective values because the filter itself scores effective values.

In profile mode, either:

- Always include effective columns, or
- Treat `--include-effective` as enabling them.

Recommendation:

- In `--mode profile`, include effective columns by default for all CSV-backed fields that participate in the profile.
- If the existing reporting code supports all fields cleanly, include effective columns for all CSV-backed fields.

Remember:

- Raw field column means what was explicitly stored in the TLV.
- Effective field column means what the filter used after applying explicit values or CSV defaults.

Source values:

```text
explicit
default
missing
invalid_default
```

---

## Important Filtering Semantics to Preserve

The trace must reflect the actual current filter behaviour:

- A variant with an excluded token is rejected before lane selection.
- Missing fields may use CSV defaults.
- A default token can itself cause rejection if it appears in the exclude list.
- If a field has multiple explicit values, the scorer uses the best applicable score for that field.
- Multi-value explicit fields do not get supplemented with the default.
- Slash lanes use bucket-local rank for lane-required fields.
- Duplicate suppression is per group and prevents the same variant being selected by later lanes.
- Ties are won by first-encountered TLV order.

Do not change selection results while adding tracing.

---

## Tests

Add host-side tests only. Do not require this feature in the Amiga build.

Minimum tests:

1. Existing filter output is unchanged when `WHDTLV_ENABLE_SELECTION_TRACE` is not defined.
2. Existing filter output is unchanged when trace is enabled but no report is requested.
3. `whdtlv_report --mode profile` marks exactly the same selected filenames as `whdtlv_filter_to_file()` outputs for the same TLV/profile/search.
4. Single-lane profile produces one `X` per selected group.
5. Non-winning variants in a selected group are marked `-` with `reason_code=lost_score` where appropriate.
6. A variant rejected by an exclude rule is marked `R` with `reason_code=rejected_exclude`.
7. Multi-lane slash profile includes `selection_lane` and `lane_requirements`.
8. Multi-lane profile can produce multiple `X` rows in the same group, one per satisfied lane.
9. Duplicate-suppressed variants are marked `D` if the trace can observe them.
10. A missing language field using `Language.csv` default `En` shows `language_effective=En` and `language_effective_source=default`.
11. An explicit multi-language field such as `De;Fr` remains explicit and does not add `En` by default.
12. Search pattern `lotus*` reports only matched groups in v1, or marks skipped groups with `S` if that option is implemented.
13. CSV output escapes commas, quotes, and semicolons correctly.
14. Profile mode output can be opened in Excel and sorted by `group_id`, `selection_lane`, and `selected_rank`.

Regression comparison:

- Run the normal filter to produce `selected.txt`.
- Run the profile report.
- Extract rows where `selected_marker=X`.
- Confirm the selected filenames match `selected.txt` exactly, respecting multi-lane output order if applicable.

---

## Documentation Updates

Update `docs/reporting-tool.md` or the equivalent reporting documentation.

Include:

- New profile-aware report mode.
- Required `--profile` argument for profile mode.
- Optional `--search` behaviour.
- Explanation of marker values `X`, `-`, `R`, `N`, `D`, `S`.
- Explanation of `selection_lane` and `lane_requirements`.
- Explanation that profile mode uses the real filtering engine with optional trace hooks.
- Explanation that trace code is host/reporting-only and excluded from the normal WHDFetch build.
- Example command lines.
- Example CSV snippet.

Example documentation snippet:

```csv
selected_marker,selected_rank,reason_code,selection_lane,lane_requirements,group_id,group_name,display_name,score_total,language_effective,chipset_effective,memory_effective
X,1,winner,0,single-lane,145,AlienBreed2,AlienBreed2_v1.0_AGA_En,572,En,AGA,FAST2M
-,0,lost_score,0,single-lane,145,AlienBreed2,AlienBreed2_v1.0_OCS_En,422,En,OCS,FAST1M
R,0,rejected_exclude,0,single-lane,145,AlienBreed2,AlienBreed2_v1.0_CD32_En,0,En,CD32,
```

---

## Acceptance Criteria

The feature is complete when:

- `whdtlv_report` can generate a profile-aware CSV from an existing TLV and profile.
- Rows clearly identify winners using `selected_marker=X`.
- Non-winners, rejections, and lane failures are distinguishable using marker/reason columns.
- Selected rows match the output of the normal filter for the same TLV/profile/search.
- Multi-lane slash profiles show lane number and lane requirements.
- Effective/default field values are visible in profile reports.
- Normal reporting modes remain unchanged unless the new mode/options are used.
- Normal WHDFetch/Amiga builds do not include trace code unless explicitly compiled with the trace flag.
- No TLV file format changes are introduced.
- No filtering behaviour changes are introduced.
- The implementation remains schema-aware and does not assume only Games forever.

---

## Implementation Strategy

Recommended phased implementation:

### Phase 1: Inventory Existing Filter Decision Points

Inspect the current selector/scorer code. Identify where it:

- Scores a variant.
- Applies excludes.
- Applies CSV defaults.
- Builds/uses selection lanes.
- Checks lane eligibility.
- Applies duplicate suppression.
- Chooses the winner.

Write a short implementation note before changing code.

### Phase 2: Add Compile-Guarded Trace Types

Add trace enums/structs under `src/whdtlv/filtering/`, behind `WHDTLV_ENABLE_SELECTION_TRACE`.

Keep them internal.

### Phase 3: Add Trace Hooks to Real Selector

Add small trace calls at decision points. Confirm normal output is unchanged.

### Phase 4: Add Reporting CSV Writer

In `src/whdtlv/reporting/`, add code to turn trace rows into CSV rows, resolving:

- group name
- display name
- field names
- token names/descriptions
- effective values
- lane requirement strings

### Phase 5: Extend `whdtlv_report` CLI

Add the new mode and arguments.

Fail clearly if `--mode profile` is used without `--profile`.

### Phase 6: Tests and Regression Checks

Implement the tests listed above. Ensure normal filter output and existing reports are unchanged.

### Phase 7: Documentation

Update reporting documentation and include command examples.

---

## Non-Goals

Do not:

- Change the TLV file format.
- Change token ID encoding.
- Change group ID behaviour.
- Change scoring outcomes.
- Add trace/reporting structures to the normal public WHDFetch API.
- Add XLSX output in this phase.
- Add colour formatting in this phase.
- Reimplement the filter in reporting code.

---

## Final Note

The purpose of this feature is explainability. It should let a user answer:

- Why did this profile select this archive?
- Why did this other variant lose?
- Was it rejected, ineligible, duplicate-suppressed, or simply lower scoring?
- Which effective values did the filter actually use?
- Which lane selected it?
- What profile change would likely alter the result?

Keep the first implementation simple, accurate, and tied to the real selector. Accuracy matters more than an exhaustive first version.
