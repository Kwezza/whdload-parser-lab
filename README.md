
## Overview

WHDLoad archive names contain a surprising amount of useful information. A single game may exist as several archive variants: OCS, ECS, AGA, CD32, PAL, NTSC, different memory requirements, different languages, low-memory builds, enhanced editions, censored/uncensored editions, and other special cases.

For a simple downloader this creates a problem. If every matching archive is downloaded, the user can end up with several versions of the same game. That makes the collection larger, messier, and less tailored to the Amiga it is intended to run on. For example, a French user with an AGA Amiga and 8 MB of RAM may want French AGA versions where available, but fall back to English, OCS/ECS, or lower-memory versions where no better match exists.

This repository is an isolated testbed for solving that problem. It experiments with parsing WHDLoad archive filenames, recognising useful tokens, grouping likely variants of the same title, and applying profile-based selection rules to choose the best candidate from each group.

The code is kept separate from WHDFetch so the parsing and selection logic can be tested, profiled, and simplified before being integrated into the main downloader. This is especially important for classic Amiga targets, where memory use, CPU time, and allocation behaviour matter much more than they do on a modern PC.

The project currently builds both a host-side test harness and a small Amiga CLI test program. The host harness is used for fast development and regression testing, while the Amiga harness is used to check whether the compact selector is practical on real or emulated Amiga hardware.

## Source Layout

- `src/core/`
  - Parsing, grouping, profile, scoring, CSV, memory tracking, and shared utilities.
  - This folder is the portable engine code intended for constrained Amiga targets.
- `src/harness/`
  - Host and Amiga executable entrypoints and harness-only orchestration.
- `tests/`
  - Unit tests and regression scripts only.

## whdload-parser-lab

Isolated lab for WHDLoad filename parsing, grouping, profile selection, and Amiga harness validation.

## What This Repo Builds

- Host CLI parser executable: `variant_harness`
- Host test executables:
  - `tests/vh_string_pool_test` (or `.exe` on Windows)
  - `tests/vh_parse_group_key_test` (or `.exe` on Windows)
- Amiga executable: `../../Bin/Amiga/varianttest`

## Makefile Targets

Run all commands from repo root.

- `make help`
  - Prints target and variable reference.

- `make all`
  - Fast host build only.
  - Builds `variant_harness`.

- `make test-build`
  - Compile-only test build.
  - Builds `variant_harness` and both host test executables.
  - Does not execute tests.

- `make test`
  - Full host test flow.
  - Runs `make test-build`, then runs tests.
  - On Windows, delegates execution to `tests/run_tests_windows.bat`.

- `make varianttest-amiga`
  - Cross-builds the Amiga binary.
  - Requires vbcc toolchain (`vc`) and include paths.

- `make clean`
  - Removes host/amiga build artifacts.

## Useful Make Variables

- `CC=compiler`
  - Host C compiler override (default: `gcc`).

- `CFLAGS=flags`
  - Host compiler flags.

- `LDFLAGS=flags`
  - Host linker flags.

- `AMIGA_CC=compiler`
  - Amiga compiler override (default: `vc`).

- `AMIGA_AUTO=0` or `AMIGA_AUTO=1`
  - Controls `-lauto` link flag for Amiga builds (default: `1`).

Example:

```sh
make varianttest-amiga AMIGA_AUTO=0
```

## PC Build And Test

### 1. Fast local compile

```sh
make all
```

### 2. Build tests without running

```sh
make test-build
```

### 3. Run full host test suite

```sh
make test
```

What this includes:

- Unit tests:
  - `tests/test_vh_string_pool.c`
  - `tests/test_vh_parse_group_key.c`
- Regression script:
  - `tests/run_milestone4_tests.ps1`
- Generated outputs in `test_output/`

### 4. Manual host CLI smoke checks

```sh
./variant_harness --help
./variant_harness --parse "SomeGame_v1.0.lha"
./variant_harness --select tests/filenames_basic.txt --profile default
./variant_harness --report tests/filenames_basic.txt --profile default
```

On Windows PowerShell, use `./variant_harness.exe` if needed.

## Amiga Build And Test

### Prerequisites

- vbcc available and `vc` callable.
- `VBCC` environment variable set so include path resolves:
  - `$(VBCC)/targets/m68k-amigaos/include`
- NDK and Roadshow include paths valid for your machine:
  - `NDK_INC` (default: `C:/Amiga/AmigaIncludes`)
  - `ROADSHOW_INC` (default: `C:/Amiga/Roadshow-SDK-1.8/netinclude`)

### 1. Build Amiga binary

```sh
make varianttest-amiga
```

Output binary:

- `../../Bin/Amiga/varianttest`

### 2. Copy and run on Amiga

Run the binary from a location that can access profiles/defs and DAT input file.

`src/harness/amiga_varianttest.c` resolves defaults from:

- defs dir candidates (for CSVs):
  - `data/Defs`
  - `tools/variant_harness/data/Defs`
  - `PROGDIR:data/Defs`
  - `PROGDIR:tools/variant_harness/data/Defs`
- profile candidates:
  - `data/Profiles`
  - `tools/variant_harness/data/Profiles`
  - `PROGDIR:data/Profiles`
  - `PROGDIR:tools/variant_harness/data/Profiles`
- DAT candidates:
  - `temp/Dat files/Games(2026-04-17).txt`
  - `Bin/Amiga/temp/Dat files/Games(2026-04-17).txt`
  - `PROGDIR:temp/Dat files/Games(2026-04-17).txt`

Usage on Amiga:

```text
varianttest [DATFILE] [PROFILE]
```

### 3. Check Amiga outputs

The Amiga harness writes:

- summary report:
  - `PROGDIR:varianttest_result.txt` (fallback `varianttest_result.txt`)
- selected list:
  - `PROGDIR:varianttest_selected.txt` (fallback `varianttest_selected.txt`)

A successful run includes `result: success` in the summary file.

## Typical Workflows

### Fast code/build iteration (no test run)

```sh
make all
make test-build
```

### Full host verification before commit

```sh
make test
```

### Build host + Amiga artifacts

```sh
make all
make varianttest-amiga
```
