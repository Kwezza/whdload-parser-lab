# Harness Commands — `src/harness/main.c`

This document describes each command exposed by the `variant_harness` CLI, what it does internally, and why it exists in the context of the WHDLoad variant-selection development and test pipeline.

---

## Overview

`main.c` is the host-side CLI harness for the variant-selection engine. It is not a unit test runner — it is a thin command dispatcher that wires CLI arguments to the core library functions in `src/core/`. The test scripts in `tests/` drive it to exercise the engine and compare output against expected baselines.

All commands read their data from paths relative to the repository root (`data/Defs/` for CSV definitions, `data/Profiles/` for profiles). The harness must be run from the repository root.

---

## Commands

### `--parse <filename>`

**Handler:** `handle_parse()`

Parses a single WHDLoad archive filename (e.g. `GameTitle_v1.2_AGA_En.lha`) using `vh_parse_filename()` and prints every field the parser extracted.

**Output fields:**

| Field | Description |
|---|---|
| `archive` | The raw archive name as supplied |
| `title` | Human-readable title extracted from the filename |
| `group_key` | Lower-case alphanumeric key used to group variants together |
| `version` | Version string if detected |
| `chipset` | Chipset tokens (e.g. `AGA`, `ECS`) |
| `memory` | Memory tokens (e.g. `1MB`, `SlowMem`) |
| `language` | Language tokens (e.g. `En`, `Fr`, `De`) |
| `video` | Video standard tokens (e.g. `PAL`, `NTSC`) |
| `media` | Media type tokens (e.g. `Disk`) |
| `special` | Recognised special tags (e.g. `NoSplash`, `Fix`) |
| `unknown` | Tokens that did not match any known category |

**Why it exists:**  
Used during Milestone 2 development to verify the parser's tokenisation and grouping key generation on individual filenames. Still used by the test scripts to confirm that edge-case filenames (filenames resembling demos, sequels, editors, data disks) do not get incorrectly merged into the same group as the base game. The `bad_grouping_parse_*` test entries in `run_milestone4_tests.ps1` rely on this command.

---

### `--resolve <field> <token>`

**Handler:** `handle_resolve()`

Looks up a single token in the CSV definition file for the named field and prints whether it matched and what the canonical form is.

**Supported fields:** `Memory`, `Language`, `Chipset`, `Video`, `Media`

**Output fields:**

| Field | Description |
|---|---|
| `field` | Display name of the field |
| `token` | The token that was searched for |
| `matched` | `yes` or `no` |
| `id` | Numeric definition ID (only when matched) |
| `canonical` | The canonical token string (only when matched) |
| `description` | Human-readable description of the value (only when matched) |

**Lookup behaviour:**

1. Exact token match against the CSV is tried first.
2. Case-insensitive fallback is tried second.
3. Duplicate-ID aliases are supported — multiple tokens can resolve to the same canonical entry.

**Why it exists:**  
Introduced in Milestone 1 to validate that the CSV loading and token resolution pipeline was working correctly before the full parser existed. In the test suite it is used by:

- `run_resolve_smoke.ps1` — a quick sanity check across all fields to confirm basic lookup is intact.
- `run_milestone4_tests.ps1` — specifically tests memory aliases `Slow` and `SlowMem`, whose canonical mappings are compared against `tests/expected/memory_alias_slow.resolve` and `tests/expected/memory_alias_slowmem.resolve`.
- The memory resolve sweep in `run_milestone4_tests.ps1` also exercises a broader set of memory tokens (`512k`, `1MB`, `1MbChip`, `LowMem`, `Fast`, etc.) to confirm no regressions in alias coverage.

The exit code is `0` on a successful match and `1` on no match, allowing scripts to distinguish between the two without parsing output.

---

### `--select <listfile> --profile <name|path> [--memtrace]`

**Handler:** `run_selection()` with `report_mode = 0`

Reads a list of archive filenames from `<listfile>`, groups them by their parsed `group_key`, scores each group under the given profile, and prints exactly one winner per group. Singleton groups (a single candidate with no alternatives) are auto-selected without scoring.

**Output:** One archive name per line, preserving the original input order of winning candidates.

**Profile resolution:**  
If `<name>` contains no path separator or `.`, it is expanded to `data/Profiles/<name>.profile`. Otherwise it is used as a literal path, which allows test-specific profiles to be supplied (e.g. `tests/profiles/phase1_singleton_exclude.profile`).

**Why it exists:**  
This is the primary output mode of the selection engine. The `--select` tests confirm that:

- Duplicate groups produce exactly one winner.
- Singleton groups are passed through unchanged.
- The winning candidate is determined by profile weights and scoring, not by arbitrary ordering.
- The output order matches the original input order (deterministic, stable).
- Profile-based exclusions (`exclude.*`) correctly suppress candidates, including causing a singleton to appear unselected when the only candidate is excluded.

Key regression baselines tested against expected files:

| Test name | Input | Profile | Expected baseline |
|---|---|---|---|
| `grouping_false_positive_select` | `tests/grouping_false_positive_cases.txt` | `default` | `tests/expected/grouping_false_positive.select` |
| `games_small_real` (Phase 2) | `tests/dat_samples/games_small_real.txt` | `default` | `tests/expected/games_small_real.select` |
| `demos_small_real` (Phase 2) | `tests/dat_samples/demos_small_real.txt` | `default` | `tests/expected/demos_small_real.select` |
| `singleton_exclude_select` | `tests/singleton_profile_exclude_cases.txt` | `phase1_singleton_exclude` | `tests/expected/singleton_profile_exclude.report` |

---

### `--report <listfile> --profile <name|path> [--memtrace]`

**Handler:** `run_selection()` with `report_mode = 1`

Runs the same grouping and selection pipeline as `--select` but instead of printing only the winners, it prints a structured report for every group that has more than one candidate, was rejected, or produced no selection. Singleton groups that are cleanly selected are omitted from the report to reduce noise.

**Per-group report includes:**

- **Group key** — the normalised grouping key shared by all candidates in the group.
- **Selected** — the winning candidate and its score.
- **Skipped** — all non-winning candidates, each with their score, and a rejection reason if they were explicitly rejected by the profile (with the reject category: `language`, `chipset`, `memory`, `video`, `media`, `special`, `profile`, or `parse`).
- **Recognised special tags** — all `special` tokens appearing across the group's candidates.
- **Unknown tokens** — all unrecognised tokens across the group's candidates.

After all groups, the report also prints a **memory estimate block** (`print_memory_estimate()`) that details the approximate heap usage of the current run at each stage of the data model.

**Why it exists:**  
The report mode is the primary diagnostic tool during development. It makes it possible to see exactly why a candidate won or lost within a group, and to spot unknown tokens that may need to be added to the CSV definitions. It is also used to:

- Validate language-handling behaviour against `tests/expected/language_cases.report`.
- Validate singleton-exclusion behaviour against `tests/expected/singleton_profile_exclude.report`.
- Verify that the memory estimate block is present in the output (Phase 3 assertion) and contains the expected fields: `candidate_count`, `peak_memory_estimate_bytes`, `candidate_lite_struct_size_bytes`, `candidate_lite_array_bytes`, `order_array_bytes`, `estimated_selector_peak_bytes`.
- Confirm the stress dataset processes at least 120 candidates without error (Phase 3 stress assertion against `tests/dat_samples/games_stress_large.txt`).

---

### `--memtrace` (flag for `--select` and `--report`)

**Handler:** `print_memtrace_summary()` (appended after selection output)

When supplied alongside `--select` or `--report`, captures heap statistics from `vh_memtrack` and prints a detailed allocation trace after the main output. The trace records:

- Actual peak heap usage in bytes
- Current heap before and after cleanup (to confirm full deallocation)
- Total allocation, free, and realloc counts
- Largest single allocation with its tag name and operation
- Per-tag breakdown of malloc/realloc counts and peak/current bytes
- Whether all tracked allocations were freed by program end (`all_tracked_allocations_freed`)

**Why it exists:**  
The Amiga target has extremely limited memory. The memtrace flag lets the harness be used to measure and validate the real heap overhead of the selection pipeline on representative datasets. It is used in combination with the memory estimate block in `--report` to cross-check the analytical estimate against the actual tracked allocations.

---

## Internal Utilities (not directly invokable)

### `print_report()` / `print_selected_candidates()`

These are called by `run_selection()` depending on the mode. `print_selected_candidates()` iterates the candidate list and prints only items where `candidate->selected` is true. `print_report()` sorts candidates by group using an in-place heap sort (`sort_order_by_group()`) so that group members can be printed contiguously without requiring a separate grouping data structure.

### `sort_order_by_group()` / `compare_index_by_group()`

An in-place heap sort on an integer index array ordered by `(group_hash, group_key, original_index)`. The three-part comparison key ensures deterministic output: hash collisions are resolved by full string comparison, and ties within a group fall back to the original input position.

### `collect_group_report_tokens()`

For a contiguous run of candidates belonging to the same group, this re-parses each candidate filename and accumulates all `special` and `unknown` tokens into temporary string pools, which are then printed as the per-group recognised/unknown token lines in the report. Re-parsing is deliberate: the candidate structs do not store token text, only scored ID lists.

### `print_memory_estimate()`

Computes a breakdown of estimated heap usage from the loaded candidate list and prints it as part of the `--report` output. Also calculates the estimated footprint of the previous (old) data model using the constant `VH_OLD_MODEL_CANDIDATE_STRUCT_SIZE_BYTES = 4908` bytes per candidate, and reports the percentage reduction achieved by the current model.

Disabled (stub printed) when compiled with `VH_AMIGA_MINIMAL`.

---

## Related Test Files

| File | Purpose |
|---|---|
| `tests/run_milestone4_tests.ps1` | Main test runner; drives all four commands and compares against baselines |
| `tests/run_resolve_smoke.ps1` | Lightweight smoke test for `--resolve` across all fields |
| `tests/expected/*.select` | Baseline expected output for `--select` runs |
| `tests/expected/*.report` | Baseline expected output for `--report` runs |
| `tests/expected/*.resolve` | Baseline expected output for `--resolve` runs |
| `tests/dat_samples/` | Real-world WHDLoad filename samples used in Phase 2 and Phase 3 tests |
| `tests/profiles/` | Test-specific profiles (e.g. singleton exclusion profile) |
