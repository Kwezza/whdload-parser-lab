# dat_to_tlv

WHDLoad archive names contain a surprising amount of useful information. A single game may exist as several archive variants: OCS, ECS, AGA, CD32, PAL, NTSC, different memory requirements, different languages, low-memory builds, enhanced editions, censored/uncensored editions, and other special cases.

For a simple downloader this creates a problem. If every matching archive is downloaded, the user can end up with several versions of the same game. That makes the collection larger, messier, and less tailored to the Amiga it is intended to run on. For example, a French user with an AGA Amiga and 8 MB of RAM may want French AGA versions where available, but fall back to English, OCS/ECS, or lower-memory versions where no better match exists.

This repository is an isolated testbed for solving that problem. It experiments with parsing WHDLoad archive filenames, recognising useful tokens, grouping likely variants of the same title, and applying profile-based selection rules to choose the best candidate from each group.

The code is kept separate from WHDFetch so the parsing and selection logic can be tested, profiled, and simplified before being integrated into the main downloader. This is especially important for classic Amiga targets, where memory use, CPU time, and allocation behaviour matter much more than they do on a modern PC.

The project currently builds both a host-side test harness and a small Amiga CLI test program. The host harness is used for fast development and regression testing, while the Amiga harness is used to check whether the compact selector is practical on real or emulated Amiga hardware.

## What The Tool Does

The current executable converts a Logiqx-style DAT file into one aggregate TLV file.

At a high level the built tool does this:

1. Reads the DAT XML file.
2. Extracts each `<rom name="..."/>` filename.
3. Parses each filename into structured metadata fields.
4. Validates tokens against CSV lookup tables.
5. Writes one binary TLV stream containing all parsed records.

Default paths baked into the executable:

- DAT input: `assets_raw/Games(19-05-2025).dat`
- TLV output: `output/Games(19-05-2025).tlv`
- CSV definitions: `assets_raw/defs`
- Pack type configuration: `assets_raw/prefs/pack_types.ini`

## Pipeline Overview

The code that is actually built by the current `Makefile` implements this path:

```text
DAT parser -> filename processor -> CSV/token validation -> dynamic field registry -> TLV writer
```

The main ownership split is:

- `app_src/`: standalone program entry point and minimal DAT parsing.
- `src_raw/`: active TLV pipeline logic and related support modules extracted from the parent project.
- `src/`: stable platform wrappers and utility code used by the pipeline.
- `include_raw/` and `include/`: public headers for those two source trees.

## Folder Map

### Root

- `Makefile`: defines the host and Amiga builds and lists the source files that are currently compiled.
- `README.md`: project overview and folder ownership map.
- `AGENTS.md`: repository-specific instructions for coding agents.
- `benchmark-summary*.txt`: saved benchmark output from earlier runs.
- `dat_to_tlv`: checked-in binary/artifact from prior work.

### `app_src/`

This is the standalone harness layer for the converter.

- `main.c`: the real entry point for the tool. It parses command-line arguments, initializes logging, loads the DAT filenames, starts the TLV session, aggregates results, writes the output file, and prints benchmark/summary information.
- `dat_parser_minimal.c` and `dat_parser_minimal.h`: a deliberately small DAT reader that scans the XML and extracts `<rom name="...">` attributes into an in-memory filename list. This is the first stage of the current pipeline.

### `src_raw/`

This folder contains the extracted feature code that actually understands WHDLoad naming and TLV generation. It is the most important folder for the conversion system.

Modules used by the current build:

- `filename_processor.c`: the filename orchestration engine. It sanitizes raw names, tokenizes them, detects language/version patterns, matches tokens against CSV data, and emits typed fields into a TLV record.
- `field_registry.c`: builds the runtime field registry from `pack_types.ini`. Field IDs are assigned dynamically here, which is why the TLV writer can embed a metadata map instead of relying on fixed numeric IDs.
- `csv_cache.c`: loads and caches CSV lookup tables from `assets_raw/defs`, performs fast token lookups, and handles CSV-backed metadata validation.
- `tlv_builder.c`: owns TLV record allocation, entry insertion, metadata-map writing, aggregate file writing, and batch/session processing.
- `error_handling.c`: shared error container and formatting helpers used across the processing pipeline.
- `tlv_profile.c`: optional profiling instrumentation used when building with `PROFILE=1`.
- `slug_util.c`: helper routines for slug-like string handling used by the extracted pipeline code.

Modules present in the repo but not compiled by the current `Makefile`:

- `variant_iterator.c` and `variant_index.c`: interpret an aggregate TLV record as a sequence of variant records and build searchable per-variant descriptors.
- `active_set.c`: bitmap-based selection/filtering state over variant indices.
- `filter_profile.c`: builds weighted include/exclude scoring rules from preferences and field defaults.
- `filter_pipeline.c`: higher-level filter pipeline that combines indexing, active-set filtering, search, and score ordering.
- `filter_runtime.c`: runtime wrapper for loading a TLV snapshot, reconstructing field mappings, loading a profile, and scoring variants.
- `profile_loader.c`: profile file loader for the staged selector/filter configuration.

That second group is support code for the planned variant-selection system. It matters to the broader TLV workflow, but it is not part of the executable produced by the current `Makefile` yet.

### `src/`

This folder holds reusable support code that the TLV pipeline depends on rather than domain-specific parsing logic.

- `platform/platform_io.c`: cross-platform wrappers for file I/O, directory scanning, directory creation, and similar filesystem operations on host and Amiga.
- `platform/platform_string.c`: Amiga-compatible replacements for string helpers such as case-insensitive comparison and re-entrant tokenization.
- `io/pack_types_loader.c`: strict parser and validator for `pack_types.ini`, including validation of field lists and pack metadata constraints.
- `io/writeLog.c`: runtime logfile support used by the standalone tool and by the pipeline modules.
- `utils/prettify.c`: display-name beautification and override loading used when raw archive names need a cleaner human-facing title.

### `include/`

Headers for the stable support layer in `src/`.

- `platform.h`: platform-wide types, macros, and compile-time environment guards.
- `platform/`: declarations for platform I/O and string helpers.
- `io/`: declarations for logging.
- `utils/`: declarations for helper utilities such as name prettification.
- `tlv_filename/tlv_profile.h`: profiling interfaces shared with the pipeline.
- `integration/whdtlv_integration.h`: **public facade header**. Normal callers include only
  this header. It exposes `whdtlv_build_from_dat`, `whdtlv_build_options_defaults`, and the
  `WhdTlvBuildOptions`/`WhdTlvBuildSummary` structs. No internal types leak through it.

### `include_raw/`

Headers for the extracted TLV and filter code in `src_raw/`.

- `tlv_filename/`: declarations for the active DAT-to-TLV path, including the filename processor, CSV cache, error system, field registry, metadata handling, and TLV builder.
- `filter/`: declarations for the staged variant indexing, filtering, and profile system.
- `io/pack_types_loader.h`: interface shared with the runtime registry/pack-type code.

### `assets_raw/`

Runtime data files used by the converter and by the staged filter system.

- `defs/`: CSV lookup tables. These map tokens such as chipset, memory, language, video, media, and contributor tags to numeric IDs and canonical meanings.
- `prefs/pack_types.ini`: the configuration that defines pack types and which fields are active in the registry.
- `prefs/prefs.ini`: broader preferences used by staged filtering/profile code.
- `profiles/`: profile definitions for future variant filtering and ranking work.

These assets are not just reference material. The pipeline depends on them at runtime.

### `output/`

Generated TLV files and captured benchmark outputs from local runs. This is where the default conversion output is written.

### `docs/`

Project notes, handover documents, and benchmark analysis. This is the best place to look for known runtime issues, performance observations, and milestone context.

### `notes/`

Backport and staging notes. In particular, `backport_inventory.md` explains which extracted files are required for Milestone 1 versus later selector work.

## Embedding the Converter

To call the converter from another project, include only the public facade header:

```c
#include <integration/whdtlv_integration.h>

WhdTlvBuildOptions opts;
WhdTlvBuildSummary summary;
whdtlv_build_options_defaults(&opts);

int rc = whdtlv_build_from_dat(
    "path/to/input.dat",
    "path/to/defs",
    "path/to/pack_types.ini",
    "path/to/output.tlv",
    0,       /* pack_type_id: 0 = all types */
    &opts,
    &summary
);
```

The facade is implemented in `src_raw/whdtlv_integration.c`. No internal headers need to be
included by the caller. See `TLV_INTEGRATION_GUIDE.md` for the full session API and filtering
documentation.

## Which Code Is Responsible For TLV Creation

If you want to understand the current converter end-to-end, these are the key pieces in order:

1. `app_src/main.c`
	Orchestrates the run, owns CLI/default paths, and calls into the TLV session API.
2. `app_src/dat_parser_minimal.c`
	Extracts archive filenames from the DAT.
3. `src/io/pack_types_loader.c`
	Reads `pack_types.ini`, which defines what fields exist and how pack types behave.
4. `src_raw/field_registry.c`
	Turns those field definitions into the runtime registry used throughout the pipeline.
5. `src_raw/csv_cache.c`
	Loads and caches the token definition CSV files.
6. `src_raw/filename_processor.c`
	Converts a raw archive name into typed metadata fields.
7. `src_raw/tlv_builder.c`
	Stores those fields as TLV entries and writes the final aggregate file.
8. `src/platform/*` and `src/io/writeLog.c`
	Provide the filesystem, string, and logging support that makes the same logic run on both host and Amiga targets.

## Which Code Is Responsible For TLV Filtering

The filtering system reads a finished TLV file, reconstructs the field registry from the embedded metadata map, builds an in-memory variant index, and applies profile-based scoring to rank or select variants. It operates entirely on the output of the TLV creation step and has no dependency on the DAT file.

All of these modules are present in the repository. None of them are compiled by the current `Makefile`. To use them they must be added to your build.

The pieces in order:

1. `src_raw/variant_iterator.c`
	Walks the flat TLV entry array and detects the boundaries between individual variant records. Each variant starts with a `display_name` entry and is followed by its associated field entries. The iterator exposes one variant at a time without allocating an index.
2. `src_raw/variant_index.c`
	Builds the in-memory `TLV_VariantIndex` from a loaded `TLV_Record`. It calls the iterator internally and for each variant creates a `TLV_VariantDescriptor` that caches the display name, a normalized base name with version suffixes stripped, pre-computed FNV-1a hashes of both names, and up to 16 captured field tokens. The index is the primary data structure used by all downstream filter operations.
3. `src_raw/active_set.c`
	Maintains a bitset over the variant index. All variants start active. Filter operations deactivate variants that do not match criteria. The active set can be queried as a dense list of active indices, rebuilt on demand after each filter pass.
4. `src_raw/filter_profile.c`
	Builds a `FilterProfile` from named include/exclude token lists and per-field weights. The profile assigns a score to each active variant by comparing its captured tokens against the include/exclude lists. Variants whose tokens appear on an exclude list are rejected and removed from the active set.
5. `src_raw/filter_pipeline.c`
	The top-level orchestration layer. `filter_pipeline_build` calls `variant_index_build` and `active_set_init`. `filter_pipeline_apply_profile` runs the scoring pass. `filter_pipeline_get_sorted` returns a dense array of variant indices in descending score order. `filter_pipeline_apply_field_filter` applies structural include/exclude filtering on a named field. `filter_pipeline_apply_search` runs a substring search over active variant display names.
6. `src_raw/filter_runtime.c`
	A convenience facade that owns a `FieldRegistry`, `GlobalCSVManager`, and `FilterProfile` in one struct. `filter_runtime_init` loads the registry and CSVs. `filter_runtime_build_profile` accepts chipset, language, and memory constraints as comma-separated token strings and builds the scoring profile. `filter_runtime_load_snapshot` opens a TLV file, reads the embedded metadata map to reconstruct field IDs, and calls `variant_index_build`. `filter_runtime_score_all` runs the profile scoring pass and returns the count of active variants.
7. `src_raw/profile_loader.c`
	Loads profile definitions from files in `assets_raw/profiles/`. Profiles are named preference sets that pre-configure the include/exclude lists for common Amiga hardware targets such as AGA-only or legacy OCS/ECS.

Headers for all of these are in `include_raw/filter/`.

## Build

The Makefile uses `cmd` as its shell. Do not use PowerShell inside the Makefile rules.

Build the host version:

```bat
make
```

Explicit host build:

```bat
make TARGET=host
```

Amiga build:

```bat
make TARGET=amiga
```

Enable TLV profiling instrumentation:

```bat
make PROFILE=1
```

Shortcut targets:

```bat
make host
make amiga
make run
make clean
make help
```

Build outputs:

- `build/host/dat_to_tlv.exe`
- `build/amiga/dat_to_tlv`

## Run

Run the host build directly:

```bat
build\host\dat_to_tlv.exe
```

Or build and run via make:

```bat
make run
```

Command-line syntax:

```text
dat_to_tlv[.exe] [--no-log] [dat_path output_path [csv_dir pack_types_ini]]
```

Examples:

```bat
build\host\dat_to_tlv.exe
build\host\dat_to_tlv.exe assets_raw\Games(19-05-2025).dat output\games_test.tlv
build\host\dat_to_tlv.exe assets_raw\Games(19-05-2025).dat output\games_test.tlv assets_raw\defs assets_raw\prefs\pack_types.ini
build\host\dat_to_tlv.exe --no-log
```

On Amiga, raise the stack before running, for example:

```text
STACK 100000
```

## Current Scope And Status

- The current shipped build is focused on DAT-to-TLV conversion.
- The filtering and scoring stack (`variant_iterator.c`, `variant_index.c`, `active_set.c`, `filter_profile.c`, `filter_pipeline.c`, `filter_runtime.c`, `profile_loader.c`) is complete and present in `src_raw/` but is not wired into the current `Makefile` build target.
- Host builds work. The Amiga binary is known to crash at runtime and still needs investigation.
- The runtime field registry is dynamic, so TLV files embed a metadata map (block `0x01`) that lets field IDs be reconstructed later without reprocessing the original DAT.

## Expected Summary Output

Successful runs print a summary like this:

```text
DAT input:      assets_raw/Games(19-05-2025).dat
Output TLV:     output/Games(19-05-2025).tlv
CSV folder:     assets_raw/defs
Pack types:     assets_raw/prefs/pack_types.ini
DAT entries:    3861
Processed:      3861
Successful:     3861
Errors:         0
TLV entries:    11634
TLV build time: <n> ms
TLV save time:  <n> ms
```
