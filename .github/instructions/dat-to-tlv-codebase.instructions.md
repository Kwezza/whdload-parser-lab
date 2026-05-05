  ---
description: "Use when modifying dat_to_tlv C code, headers, Makefile rules, TLV pipeline modules, or project documentation. Covers repo architecture, build constraints, and what is currently in scope versus staged future work."
name: "dat_to_tlv Codebase Conventions"
applyTo: "README.md, AGENTS.md, Makefile, app_src/**, src/**, src_raw/**, include/**, include_raw/**, docs/**, notes/**"
---
# dat_to_tlv Codebase Conventions

- Treat this repository as a standalone DAT-to-TLV staging area extracted from the parent downloader, not as a generic C project.
- Keep documentation and code comments aligned with the current repository layout and the current `Makefile`. Do not refer to old staging paths such as `variant_backport_staging`.

## Architecture split

- `app_src/` is the standalone harness layer. `main.c` is the entry point and `dat_parser_minimal.c` is the DAT filename extractor.
- `src_raw/` is the active extracted pipeline area. This is where TLV parsing, field registry, CSV cache, and TLV builder work lives.
- `src/` is the stable support layer for platform I/O, string helpers, logging, and utility code.
- `include_raw/` mirrors `src_raw/`. `include/` mirrors `src/`.

## Current build scope

- The current shipped build is DAT parse -> filename processing -> CSV validation -> field registry -> TLV output.
- The `Makefile` currently compiles: `app_src/main.c`, `app_src/dat_parser_minimal.c`, `src_raw/error_handling.c`, `src_raw/field_registry.c`, `src_raw/tlv_profile.c`, `src_raw/csv_cache.c`, `src_raw/filename_processor.c`, `src_raw/tlv_builder.c`, `src/platform/platform_io.c`, `src/platform/platform_string.c`, `src/io/writeLog.c`, `src/io/pack_types_loader.c`, and `src/utils/prettify.c`.
- `filter_*`, `variant_*`, `active_set.c`, `profile_loader.c`, and related headers are staged selector/filter work. Do not describe them as active runtime behavior unless you also wire them into the current build.

## Build and platform rules

- The `Makefile` uses `cmd` as its shell. Do not introduce PowerShell-based build steps or PowerShell snippets into normal make rules.
- Keep host and Amiga builds working through the existing target split: host uses GCC, Amiga uses vbcc.
- Use the existing platform guards only:

```c
#ifdef PLATFORM_AMIGA
    /* Amiga-specific code */
#else
    /* Host code */
#endif
```

- Avoid C99-only syntax in `src_raw/` modules because the Amiga target is built in vbcc C89 mode.

## TLV pipeline ownership

- `src/io/pack_types_loader.c` defines and validates pack type configuration from `assets_raw/prefs/pack_types.ini`.
- `src_raw/field_registry.c` turns pack type definitions into runtime field IDs.
- `src_raw/csv_cache.c` owns CSV loading and token lookup from `assets_raw/defs/`.
- `src_raw/filename_processor.c` converts raw archive names into structured metadata fields.
- `src_raw/tlv_builder.c` owns TLV record creation, batch/session processing, and writing TLV output with embedded metadata.
- `src/platform/*` and `src/io/writeLog.c` are support layers, not the domain parsing logic.

## Documentation expectations

- When updating docs, explain which folders are active conversion code, which are support layers, and which are staged future selector work.
- When describing defaults, use the current runtime paths:
  - DAT input: `assets_raw/Games(19-05-2025).dat`
  - TLV output: `output/Games(19-05-2025).tlv`
  - CSV definitions: `assets_raw/defs`
  - Pack types: `assets_raw/prefs/pack_types.ini`
- Call out the known Amiga runtime issue accurately: host builds work, but the Amiga binary is still known to crash at runtime.

## Editing guidance

- Prefer minimal edits that preserve the current split between harness code, extracted pipeline code, and stable support code.
- If you add a new pipeline module, add the `.c` file in `src_raw/`, its header in `include_raw/`, and update the `SRC` list in `Makefile`.
- If you change runtime behavior that depends on CSVs or pack type configuration, keep `assets_raw/` documentation in sync.