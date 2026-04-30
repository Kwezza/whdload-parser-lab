# variant_harness

PC-side GCC harness for WHDFetch variant-selection backport work.

## Completed Milestones

The harness and staging work completed so far maps to Milestones 0 through 4 in the task plan.

## Phase 3 Kickoff Status

Status: started

- Report output now includes a memory estimate summary with:
	- candidate count
	- average filename length
	- candidate struct size estimate
	- parsed token storage estimate
	- peak memory approximation for the processed list
- `make test` now validates that memory estimate output is present in:
	- `test_output/phase3_games_memory_report.txt`

### Milestone 0: Backport staging pack

Status: completed

- Staging inventory and classifications exist under `variant_backport_staging/notes/backport_inventory.md`.
- Milestone-relevant assets and source references were copied and categorized.
- `Special.csv` was staged and preserved for report visibility and future weighting.

### Milestone 1: PC harness scaffold and CSV resolver

Status: completed

- GCC harness scaffold created under `tools/variant_harness`.
- Runtime data layout is in place:
	- `data/Defs`
	- `data/Profiles`
	- `data/pack_types.ini`
- `--resolve <field> <token>` implemented for:
	- Memory
	- Language
	- Chipset
	- Video
	- Media
- CSV lookup behavior implemented:
	- exact token match first
	- case-insensitive fallback second
	- duplicate ID aliases supported

### Milestone 2: Filename parsing

Status: completed

- `--parse <filename>` implemented.
- Parser behavior implemented:
	- removes `.lha` / `.lzx` extension
	- splits by `_`
	- detects versions such as `v1.3` and `1.3`
	- resolves Language, Chipset, Video, Media, Memory, Special via CSV
	- handles compact language tokens such as `FrDeEn` and `EnFrEs`
	- generates conservative `group_key` from lower-case alphanumeric title
	- preserves unmatched tokens as `unknown`

### Milestone 3: Grouping and selection

Status: completed

- `--select <listfile> --profile <name|path>` implemented.
- `--report <listfile> --profile <name|path>` implemented.
- Candidate handling implemented:
	- one archive name per line input
	- blank and `#` comment lines ignored
	- grouping by `group_key`
	- singleton groups auto-selected
	- duplicate groups scored and reduced to one winner
	- tie-break by lower original input index
	- selected output preserves original input order
- Profile loading implemented from INI-like files with include/exclude and weights.
- `special` tags are parsed and shown in report output.
- `weight.special` defaults to `0` in default profile behavior.

### Milestone 4: Test data and parity checks

Status: completed

- Core milestone test datasets added under `tests/`.
- `make test` target added in this harness Makefile.
- Automated test runner added:
	- `tests/run_milestone4_tests.ps1`
- Test artifacts are written under `test_output/`.

## Test Coverage And What It Proves

### Test inputs added

- `tests/filenames_basic.txt`
- `tests/filenames_memory.txt`
- `tests/filenames_language.txt`
- `tests/filenames_special_report_only.txt`
- `tests/filenames_bad_grouping_watchlist.txt`
- `tests/filenames_grouping.txt`
- `tests/resolve_cases.txt`
- `tests/parse_cases.txt`

### Automated runners

- `tests/run_resolve_smoke.ps1`
- `tests/run_milestone4_tests.ps1`

### Output artifacts produced

- `test_output/basic_select.txt`
- `test_output/basic_report.txt`
- `test_output/memory_report.txt`
- `test_output/language_report.txt`
- `test_output/special_report_only.txt`
- `test_output/memory_resolve.txt`
- `test_output/resolve_smoke.txt`
- `test_output/games_beta_select.txt`

### Proof statements

- Grouping correctness:
	- `basic_report.txt` and `filenames_grouping.txt` show duplicate titles grouped together while distinct title tokens remain separate.
- Selection stability:
	- `basic_select.txt` shows one selected archive per group in original input order.
- Memory token resolution parity:
	- `memory_resolve.txt` demonstrates CSV-driven handling of aliases and non-numeric memory token names.
- Compact multi-language parsing:
	- `language_report.txt` shows compact forms (for example `FrDeEn`) being expanded and scored as multi-language capabilities.
- Special tags are report-visible:
	- `special_report_only.txt` includes recognized special tags in grouped reports.
- Special tags are not scored by default:
	- with `weight.special=0` in default profile behavior, special tags do not drive score differences.
- Real DAT pipeline viability:
	- `games_beta_select.txt` is generated from `Bin/Amiga/temp/Dat files/Games-Beta(2026-04-26).txt` and demonstrates non-trivial selection at real input scale.

## Build

```sh
make
```

## Run

```sh
./variant_harness --help
./variant_harness --resolve Memory 1MbChip
./variant_harness --resolve Memory Slow
./variant_harness --resolve Language Fr
./variant_harness --resolve Chipset AGA
./variant_harness --parse AlienBreed_v1.3_AGA_1MbChip_FrDeEn_PAL.lha
./variant_harness --select tests/filenames_grouping.txt --profile default
./variant_harness --report tests/filenames_grouping.txt --profile default
```

## Run Tests

Run from `tools/variant_harness`.

```sh
make clean
make
make test
```

## Data Layout

- `data/Defs`: CSV definition files
- `data/Profiles`: profile files
- `data/pack_types.ini`: pack type configuration reference

## Current Limitations

- Selection currently uses conservative filename-based grouping only.
- Test runner is currently PowerShell-based on Windows (`make test` from harness directory).
- `--resolve`, `--parse`, `--select`, and `--report` assume runtime execution from `tools/variant_harness` so relative `data/` and `tests/` paths resolve correctly.
