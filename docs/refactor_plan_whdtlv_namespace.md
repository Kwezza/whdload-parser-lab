# Refactor Plan — whdtlv Namespace and Public Interface

**Date:** 2026-05-11  
**Branch:** main  
**Source of truth:** `docs/symbol_inventory.md` (2026-05-11)  
**Status:** Plan only. No source code is changed in this document.

---

## 1. Executive Summary

The dat-to-TLV pipeline currently lives in `src_raw/`, `src/`, and `app_src/` and is wired together by `app_src/main.c`. When this code is embedded inside WHDFetch or another host process, three problems arise:

1. **Symbol collisions.** Names like `append_to_log`, `crc32_init`, and `validate_and_split` are generic enough to collide with any other C library in the same link unit.
2. **Excessive public surface.** Internal processing steps (`filename_sanitizer_process`, `version_parser_*`, etc.) are currently declared in public headers even though they are only ever called within their own translation unit.
3. **No single clean entry point.** A caller must include `tlv_builder.h`, `filename_processor.h`, `csv_cache.h`, `field_registry.h`, `writeLog.h`, and others — and must know the session lifecycle in detail.

The intended final shape after this refactor:

- **Production code stays where it is.** `src_raw/`, `src/`, and `app_src/dat_parser_minimal.c` are not moved.
- **Internal helpers become `static`** so they generate no linker symbols at all.
- **Collision-risk shared symbols get a `whdtlv_` prefix** (or become static where possible).
- **A single facade header** is created at `include/integration/whdtlv_integration.h`. Normal callers include only that header.
- **The session lifecycle is hidden** behind a simple `whdtlv_build_from_dat(...)` call. The manual session API remains internally available but is not the primary API for WHDFetch.

Test/demo/legacy code is **not** the driver for this refactor. Changes to `tools/`, `tests/_legacy/`, and out-of-build staged files are excluded from all phases unless explicitly noted.

---

## 2. Non-Goals

This refactor must not:

- Alter the TLV wire format in any way.
- Alter parsing, scoring, or grouping behaviour.
- Alter output record order.
- Alter endian handling.
- Remove staged filter work (`filter_pipeline.c`, `filter_profile.c`, `filter_runtime.c`, `active_set.c`, `profile_loader.c`, `variant_index.c`, `variant_iterator.c`) unless explicitly instructed in a later task.
- Chase performance optimisations.
- Rename legacy or test code that is not in the default Makefile build.
- Introduce abstraction layers, plugin systems, or framework machinery.
- Rewrite or split any `.c` file for structural reasons alone.

If any phase risks touching any of the above, it must stop and flag the conflict.

---

## 3. Proposed Namespace and Visibility Policy

### Class A — Public simple facade

Caller-facing functions that will be declared in `include/integration/whdtlv_integration.h`.  
Normal callers (WHDFetch) include only this header and need nothing else.

```c
/* Phase 4: the main entry point */
int whdtlv_build_from_dat(
    const char *dat_path,
    const char *defs_dir,
    const char *pack_types_path,
    const char *output_tlv_path,
    unsigned int pack_type_id,
    const WhdTlvBuildOptions *options,
    WhdTlvBuildSummary *summary
);

/* Phase 4: option structs with sensible defaults */
void whdtlv_build_options_defaults(WhdTlvBuildOptions *opts);

/* Later — only when filter modules are wired into the Makefile */
int whdtlv_filter_to_file(
    const char *tlv_path,
    const char *profile_path,
    const char *defs_dir,
    const char *pack_types_path,
    const char *output_txt_path,
    const char *search_pattern,
    const WhdTlvFilterOptions *options,
    WhdTlvFilterSummary *summary
);
void whdtlv_filter_options_defaults(WhdTlvFilterOptions *opts);
```

The `WhdTlvBuildOptions` and `WhdTlvBuildSummary` structs are owned by the facade header. They must not expose internal struct types from `tlv_builder.h`, `field_registry.h`, or `csv_cache.h`.

### Class B — Advanced / manual session API

Available internally and to advanced callers, but not declared in the main facade header.  
If a secondary header `include/integration/whdtlv_session.h` is added later, these live there.

```c
whdtlv_session_init(...)
whdtlv_session_process_batch(...)
whdtlv_session_inject_group_ids(...)
whdtlv_write_record(...)
whdtlv_session_finalize(...)
```

These are the renamed forms of the current `tlv_session_*` and `tlv_write_record_with_metadata` functions.  
They are not part of Phase 4 planning. They exist to make `app_src/main.c` continue to compile after Phase 3 renames without requiring a facade rewrite.

### Class C — Internal shared API

Functions that cross module boundaries within the pipeline but must never appear in a caller-facing header.  
Declared in `include_raw/` headers only.

Examples (not exhaustive):

- `field_registry_alloc`, `field_registry_free`, `field_registry_get_id`, `field_registry_get_csv_basename`
- `csv_cache_manager_init`, `csv_cache_manager_cleanup`, `csv_cache_lookup`, `csv_cache_lookup_prehashed`, `csv_cache_lookup_span`
- `tlv_process_filename_orchestrator`
- `processing_error_*`
- `whdtlv_load_pack_types`, `whdtlv_free_pack_types`
- `whdtlv_prettify_init`, `whdtlv_prettify_title`, `whdtlv_prettify_shutdown`
- `whdtlv_crc32_init`, `whdtlv_crc32_update`, `whdtlv_crc32_finalize`
- `whdtlv_log_append`, `whdtlv_log_init`, `whdtlv_log_set_enabled`, `whdtlv_log_is_enabled`
- `whdtlv_derive_group_name`

After Phase 3, these carry the `whdtlv_` prefix and are therefore distinguishable, but they remain behind internal headers and are never part of the public facade.

### Class D — File-local helpers (make static)

Functions only called within their own `.c` file. They must become `static` and their declarations must be removed from all headers. See Phase 1 for the exact list.

Making these `static` is the highest-value, lowest-risk action in the entire refactor: it eliminates linker symbols with zero behaviour change.

### Class E — Dead or staged API

Functions with zero live callers (see `docs/symbol_inventory.md` §8).  
They must be kept out of public headers. Depending on their nature:

- **Staged filter work:** Leave the `.c` file, but remove or gate the declaration in the header.
- **Truly dead helpers (no future use):** Remove the declaration from the header; optionally remove the definition if certain it is not needed.
- **Read-back TLV path** (`tlv_read_*`, `tlv_has_metadata_map`, `free_csv_fingerprint_map`): Decision deferred to Phase 2. Do not remove the definitions until the team decides whether TLV read-back will be reactivated for filter loading.

---

## 4. Phase 1 — Static-Only Cleanup

**Goal:** Make all file-local functions `static`. Remove their declarations from headers.  
**Risk level:** Very low — purely additive visibility reduction.  
**Behaviour change:** None. Binary output is identical.  
**Prerequisite:** Capture a baseline TLV checksum and record count before starting.

### 4.1 Functions to Make Static

All call-graph evidence comes from the text-search analysis in `docs/symbol_inventory.md` §5.  
**Each function must be verified with a direct text search across all 15 live source files before applying `static`.** If any external caller is found, do not apply `static` and flag it for review.

#### In `src/io/pack_types_loader.c`

| Function | Header to update |
|----------|-----------------|
| `validate_and_split` | Remove declaration from `include/io/pack_types_loader.h` |

#### In `src_raw/csv_cache.c`

| Function | Header to update |
|----------|-----------------|
| `csv_direct_file_lookup` | Remove declaration from `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_manager_init_with_config` | Remove declaration from `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_is_token_in_special` | Remove declaration from `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_add_unknown_token` | Remove declaration from `include_raw/tlv_filename/csv_cache.h` |

#### In `src_raw/tlv_builder.c`

| Function | Header to update |
|----------|-----------------|
| `tlv_record_get_entry` | Remove declaration from `include_raw/tlv_filename/tlv_builder.h` |
| `tlv_record_add_field_by_name` | Remove declaration from `include_raw/tlv_filename/tlv_builder.h` |
| `tlv_write_metadata_map` | Remove declaration from `include_raw/tlv_filename/tlv_builder.h` |
| `tlv_write_csv_fingerprints` | Remove declaration from `include_raw/tlv_filename/tlv_builder.h` |
| `tlv_write_group_map` | Remove declaration from `include_raw/tlv_filename/tlv_builder.h` |

#### In `src_raw/filename_processor.c`

| Function | Header to update |
|----------|-----------------|
| `filename_sanitizer_process` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `version_parser_detect_pattern` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `version_parser_extract` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `language_parser_parse_token` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `contributor_extractor_process` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `csv_token_matcher_lookup` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |
| `csv_token_matcher_find_source` | Remove declaration from `include_raw/tlv_filename/filename_processor.h` |

#### Deferred within Phase 1

`tlv_read_metadata_map` is in `tlv_builder.c` and called only from `tlv_read_record_with_metadata`, which is itself dead. The whole read-back path is under review in Phase 2. Do not make `tlv_read_metadata_map` static in Phase 1; defer to Phase 2 when the read-back decision is made.

### 4.2 Verification Steps After Phase 1

1. Run `make TARGET=host` — must produce zero warnings.
2. Run `make TARGET=amiga` — must compile without error (link success not required if Amiga runtime is still known-broken).
3. Run the host binary against the full Games DAT. Compare output TLV file size and record count against the baseline captured before Phase 1.
4. If `nm` or equivalent is available: confirm that the functions listed in §4.1 no longer appear as global symbols in the object files.

---

## 5. Phase 2 — Bug Fixes and Header Hygiene

**Goal:** Fix the `prettify_init` latent bug, resolve the dual-declaration problem, remove dead header declarations, and decide the TLV read-back path.  
**Risk level:** Low to medium. The `prettify_init` fix may change output if name-override CSV entries start applying.  
**Prerequisite:** Phase 1 complete. Baseline TLV from Phase 1 captured.

### 5.1 Fix `prettify_init` Not Being Called

`prettify_title` and `prettify_shutdown` are called in the live build. `prettify_init` is never called. The name-override CSV (`name_overrides.csv` or equivalent) is therefore silently never loaded.

The implementing agent must:

1. Confirm which CSV file `prettify_init` is intended to load. Check `src/utils/prettify.c` for the expected file path argument.
2. Determine the correct call site — likely `tlv_session_init` in `tlv_builder.c` or `main.c` before the processing loop.
3. **Before inserting the call**, run the host binary and capture a reference TLV and log output.
4. Insert the `prettify_init(...)` call at the correct site.
5. Run the host binary again. Compare output. If output changes, document which records changed and why. This is expected behaviour — the bug fix may change titles — but must be reviewed before committing.
6. If the team decides the name-override CSV should NOT apply at this stage, then instead add a comment explaining why `prettify_init` is intentionally deferred, and mark it `/* WHDTLV_TODO: call prettify_init when override CSV is available */`.

**Do not silently swallow the fix.** Either apply it with a documented output delta, or explicitly defer it with a comment.

### 5.2 Resolve `load_pack_types` / `free_pack_types` Dual Declaration

These two functions are currently declared in both `include/io/pack_types_loader.h` and `include_raw/tlv_filename/filename_processor.h`.

The implementing agent must:

1. Confirm that the definitions live in `src/io/pack_types_loader.c` only.
2. Remove the declarations from `include_raw/tlv_filename/filename_processor.h`.
3. Add `#include "io/pack_types_loader.h"` to `src_raw/filename_processor.c` if it is not already present (it presumably includes `filename_processor.h` which had the shadow declarations).
4. Build with `-Wall -Wextra`. If any warning about redeclaration appears, fix it before proceeding.

### 5.3 Remove Internal Processing Step Declarations from `filename_processor.h`

After Phase 1, `filename_sanitizer_process`, `version_parser_*`, `language_parser_*`, `contributor_extractor_*`, `csv_token_matcher_*` are already `static` in their `.c` file. Their declarations in `filename_processor.h` must also be removed in Phase 1 (§4.1 above). This item confirms those removals are complete.

Only `tlv_process_filename_orchestrator` and `filename_processor_print_pack_field_stats` should remain declared in `filename_processor.h` after this phase.

### 5.4 Guard or Remove Dead Legacy Declarations in `tlv_builder.h`

The following are declared in `tlv_builder.h` but have zero callers:

- `build_tlv_from_dat` — labeled "legacy" in the header comment.
- `tlv_builder_create_record` — labeled "legacy interface" in the header comment.

Action:

- Wrap these declarations in `#ifdef WHDTLV_LEGACY_API` ... `#endif`.
- Do not define `WHDTLV_LEGACY_API` anywhere in the current build.
- This removes them from the effective public header without deleting the code, preserving a future escape hatch.

### 5.5 Decide the TLV Read-Back Path

The following functions are dead in the current build:

- `tlv_read_record_with_metadata`
- `tlv_has_metadata_map`
- `tlv_read_csv_fingerprints`
- `free_csv_fingerprint_map`
- `tlv_read_metadata_map` (made static candidate, deferred from Phase 1)

The implementing agent must ask: **Is the TLV read-back path needed for filter loading?**

- If **yes**: Keep definitions. Remove declarations from `tlv_builder.h`. Move declarations to a dedicated internal header `include_raw/tlv_filename/tlv_reader.h`. Make `tlv_read_metadata_map` static (it is only called from `tlv_read_record_with_metadata`).
- If **no**: Remove declarations from `tlv_builder.h`. Wrap the definitions in `#ifdef WHDTLV_READBACK_ENABLE` guards. Do not delete definitions yet — wait until filter integration is confirmed.

**This decision must be recorded in `notes/backport_inventory.md` or equivalent.**

### 5.6 Remove Other Dead Declarations from Headers

Remove header declarations (not definitions) for:

| Function | Header containing declaration |
|----------|------------------------------|
| `parse_dat_filenames_minimal` | `app_src/dat_parser_minimal.h` |
| `free_dat_filenames_minimal` | `app_src/dat_parser_minimal.h` |
| `csv_cache_reverse_lookup` | `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_update_special_csv` | `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_get_crc` | `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_get_memory_stats` | `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_report_summary` | `include_raw/tlv_filename/csv_cache.h` |
| `csv_cache_load_special_csv` | `include_raw/tlv_filename/csv_cache.h` |
| `field_registry_validate` | `include_raw/tlv_filename/field_registry.h` |
| `field_registry_has_available_ids` | `include_raw/tlv_filename/field_registry.h` |
| `field_registry_set_default` | `include_raw/tlv_filename/field_registry.h` |
| `field_registry_get_default_token_id` | `include_raw/tlv_filename/field_registry.h` |
| `field_registry_get_prescan_config` | `include_raw/tlv_filename/field_registry.h` |
| `build_field_registry_from_pack_types` | `include_raw/tlv_filename/field_registry.h` |
| `get_pack_type_by_id` | `include_raw/tlv_filename/filename_processor.h` |
| `whd_normalize_path` | `include/platform/platform_io.h` |

**Removing a declaration does not remove the definition.** The implementing agent should not delete `.c` code in this phase unless directed.

> **Caveat — staged filter work:** Some of the dead CSV and field-registry functions may be intended for the staged filter modules (`filter_pipeline.c`, `filter_runtime.c`, etc.). Before removing any declaration, confirm by searching across the staged files whether they are referenced there. If they are, add a comment `/* Used by staged filter work — do not remove declaration */` and leave the declaration in place.

### 5.7 Verification Steps After Phase 2

1. `make TARGET=host` — zero warnings.
2. `make TARGET=amiga` — compiles without error.
3. If `prettify_init` was fixed: compare output TLV and document the delta.
4. If `prettify_init` was deferred: output TLV must be byte-for-byte identical to Phase 1 baseline.
5. If `nm` is available: confirm no unexpected symbol removals from the pipeline layer.

---

## 6. Phase 3 — Collision-Risk Renames

**Goal:** Give high-collision-risk symbols the `whdtlv_` prefix.  
**Risk level:** Medium. Every rename touches definitions, declarations, call sites, and potentially include guards. Missing one call site produces a linker error, which is detectable but must be fixed immediately.  
**Prerequisite:** Phase 2 complete and verified.

### 6.1 Rename Priority and Mapping

Apply renames in this order. Higher-risk renames go first so any build break is caught early.

| Old name | New name | Risk | Reason |
|----------|----------|------|--------|
| `append_to_log` | `whdtlv_log_append` | Critical | Widest call spread; most generic name in the codebase |
| `initialize_logfile` | `whdtlv_log_init` | High | Generic init pattern |
| `set_logging_enabled` | `whdtlv_log_set_enabled` | High | Generic boolean setter |
| `is_logging_enabled` | `whdtlv_log_is_enabled` | High | Generic boolean query |
| `crc32_init` | `whdtlv_crc32_init` | High | Collides with every embedded CRC library |
| `crc32_update` | `whdtlv_crc32_update` | High | Same |
| `crc32_finalize` | `whdtlv_crc32_finalize` | High | Same |
| `prettify_init` | `whdtlv_prettify_init` | Medium | Unprefixed library noun |
| `prettify_title` | `whdtlv_prettify_title` | Medium | Same |
| `prettify_shutdown` | `whdtlv_prettify_shutdown` | Medium | Same |
| `get_csv_filename_for_field` | `field_registry_get_csv_basename` | Medium | Verb-first, no module prefix; rename improves clarity |
| `derive_group_name` | `whdtlv_derive_group_name` | Low–Medium | Unprefixed |
| `load_pack_types` | `whdtlv_load_pack_types` | Medium | Generic "load something" pattern |
| `free_pack_types` | `whdtlv_free_pack_types` | Medium | Paired with above |

### 6.2 What Must Be Updated for Each Rename

For each symbol in the table above, the implementing agent must update:

1. The **definition** in the `.c` file.
2. The **declaration** in the header file (including any `typedef`, `#define` alias, or forward declaration).
3. All **call sites** across all 15 live source files.
4. **Comments** that reference the old name by function name (not prose descriptions — only code-level comments that name the function directly).
5. **Include guards** in headers are not renamed unless the header file itself is being renamed in a later phase.

### 6.3 What Must Not Be Renamed

- **Macro-only platform abstractions** (`whd_malloc`, `whd_fopen`, etc.) are `#define` macros on the host and generate no linker symbols. Do not rename them to `whdtlv_` unless it is confirmed that they generate Amiga linker symbols and that WHDFetch uses different implementations. See `docs/symbol_inventory.md` Appendix B.
- Staged filter file symbols not in the current build. Do not touch `filter_pipeline.c`, `filter_runtime.c`, or their headers.
- Symbols already carrying a sufficiently distinctive prefix (`processing_error_*`, `tlv_profile_*`, `field_registry_*`, `csv_cache_*`) unless they appear in the collision risk table above.

### 6.4 Rename Procedure per Symbol

For each rename:

1. Perform a case-sensitive text search across all files in the repo for the old name.
2. List every hit. Categorise each as: definition, declaration, call site, comment, or unrelated occurrence.
3. Apply changes.
4. Build with `make TARGET=host`. Confirm zero warnings and zero link errors.
5. Build with `make TARGET=amiga`. Confirm no errors.
6. Commit this single rename before starting the next.

> **Warning:** Do not batch-rename multiple symbols in a single commit. If any rename breaks the build, it must be isolated.

### 6.5 Verification Steps After Phase 3

1. `make TARGET=host` — zero warnings.
2. `make TARGET=amiga` — compiles without error.
3. Run host binary against Games DAT. Output TLV must be byte-for-byte identical to Phase 2 baseline (renames do not affect behaviour).
4. If `nm` is available: confirm the old generic names (`append_to_log`, `crc32_init`, etc.) no longer appear in object files.

---

## 7. Phase 4 — Public Facade Design

**Goal:** Create `include/integration/whdtlv_integration.h` and a corresponding thin wrapper `.c` file that exposes the simple call-once API.  
**Risk level:** Low if phases 1–3 are complete. The facade is new code layered above existing internals.  
**Prerequisite:** Phase 3 complete and verified.

### 7.1 Header Location and Include Guard

```
include/integration/whdtlv_integration.h
```

Include guard: `WHDTLV_INTEGRATION_H`

The header must include only C89-compatible types. No `<stdbool.h>` unless the project already uses it. Use `int` return codes with defined constants, not `bool`.

### 7.2 Public Types

```c
/* Return codes */
#define WHDTLV_OK               0
#define WHDTLV_ERR_INVALID_ARG  1
#define WHDTLV_ERR_IO           2
#define WHDTLV_ERR_PARSE        3
#define WHDTLV_ERR_ALLOC        4

/* Options struct — zero-initialise to get defaults */
typedef struct WhdTlvBuildOptions {
    int  enable_logging;      /* 0 = off, 1 = on */
    int  enable_profile;      /* 0 = off, 1 = on (PROFILE build only) */
    int  reserved[6];         /* zero-init; reserved for future use */
} WhdTlvBuildOptions;

/* Summary struct — filled in by whdtlv_build_from_dat on success */
typedef struct WhdTlvBuildSummary {
    unsigned int records_written;
    unsigned int records_skipped;
    unsigned int groups_assigned;
    unsigned int reserved[5];
} WhdTlvBuildSummary;
```

No internal struct types (`TLV_Record`, `FieldRegistry`, `CSVCache`, etc.) must appear in this header.

### 7.3 Public Functions

```c
/* Populate opts with safe defaults. Call before whdtlv_build_from_dat. */
void whdtlv_build_options_defaults(WhdTlvBuildOptions *opts);

/*
 * Build a TLV file from a single Logiqx-style WHDLoad DAT file.
 *
 * dat_path        - path to the .dat XML file
 * defs_dir        - directory containing field CSV files (assets_raw/defs/)
 * pack_types_path - path to pack_types.ini
 * output_tlv_path - destination .tlv file (created or overwritten)
 * pack_type_id    - pack type filter (0 = all)
 * options         - caller-provided options; pass NULL for defaults
 * summary         - output summary; pass NULL to ignore
 *
 * Returns WHDTLV_OK on success, or a WHDTLV_ERR_* code on failure.
 */
int whdtlv_build_from_dat(
    const char           *dat_path,
    const char           *defs_dir,
    const char           *pack_types_path,
    const char           *output_tlv_path,
    unsigned int          pack_type_id,
    const WhdTlvBuildOptions *options,
    WhdTlvBuildSummary   *summary
);
```

### 7.4 Filter Facade — Deferred

The filter facade functions (`whdtlv_filter_to_file`, `whdtlv_filter_options_defaults`) must **not** be added to this header until the staged filter modules are wired into the Makefile. Adding stub declarations before the backing code is ready creates confusion and broken builds.

When filter modules are ready, add them to the facade header in a separate commit with accompanying documentation.

### 7.5 Implementation Location

Create `src_raw/whdtlv_integration.c`. This file:

- Includes the internal pipeline headers needed to call `tlv_session_init`, `parse_dat_entries_minimal`, `tlv_session_process_batch`, etc.
- Wraps the existing session loop from `app_src/main.c` into `whdtlv_build_from_dat`.
- Does **not** duplicate logic from `main.c` — it calls the same pipeline functions.
- Is added to the `SRC` list in the Makefile.

`app_src/main.c` should eventually be simplified to call `whdtlv_build_from_dat` once per DAT file. This is a separate task and must not be forced in Phase 4.

### 7.6 Manual Session API

The renamed session functions (`whdtlv_session_init`, etc.) remain in `tlv_builder.c` and declared in `tlv_builder.h`. They are not removed. Advanced callers who need fine-grained control can include `tlv_builder.h` directly. The facade header does not re-export them.

### 7.7 Verification Steps After Phase 4

1. `make TARGET=host` — zero warnings.
2. Build a small standalone test that includes only `<integration/whdtlv_integration.h>` and calls `whdtlv_build_from_dat`. Confirm it compiles and produces a valid TLV.
3. Run host binary (the existing `main.c` harness) against the Games DAT. Output must be byte-for-byte identical to Phase 3 baseline.

---

## 8. Phase 5 — Clean Demo Harness

**Goal:** Create a minimal, self-contained demonstration program that uses only the public facade.  
**Risk level:** Very low. New file only; no existing code is changed.  
**Prerequisite:** Phase 4 complete and verified.

### 8.1 Location

```
tools/tlv_demo/tlv_demo.c
```

### 8.2 Requirements

The demo must:

- Include **only** `<integration/whdtlv_integration.h>`.
- Call `whdtlv_build_options_defaults`, then `whdtlv_build_from_dat`.
- Print the `WhdTlvBuildSummary` fields to stdout.
- Accept `dat_path`, `defs_dir`, `pack_types_path`, and `output_tlv_path` as command-line arguments. No hard-coded paths.
- Exit with a non-zero code on failure.

The demo must **not**:

- Include `tlv_builder.h`, `csv_cache.h`, `field_registry.h`, or any internal header.
- Perform any TLV parsing, filtering, or CSV work itself.
- Become a regression test suite.

### 8.3 Makefile Integration

The demo is a separate build target, not part of the main `SRC` list. Add a `make demo` target that builds `tools/tlv_demo/tlv_demo.c` against the same object files as the main build.

Do not modify the default `make` target or the `SRC` list for the main binary.

### 8.4 Verification Steps After Phase 5

1. `make demo TARGET=host` — compiles without warnings.
2. Run the demo against a small DAT file. Confirm output TLV is identical to what the main binary produces.

---

## 9. Testing Plan

### After Each Phase

| Check | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 5 |
|-------|---------|---------|---------|---------|---------|
| `make TARGET=host` zero warnings | ✓ | ✓ | ✓ | ✓ | ✓ |
| `make TARGET=amiga` compiles | ✓ | ✓ | ✓ | ✓ | ✓ |
| Output TLV byte-identical to baseline | ✓ | ✓* | ✓ | ✓ | ✓ |
| Small DAT smoke test | ✓ | ✓ | ✓ | ✓ | ✓ |
| Full Games DAT if practical | ✓ | ✓ | ✓ | ✓ | — |
| `nm` confirms old generic names absent | — | — | ✓ | — | — |
| Demo compiles with facade header only | — | — | — | ✓ | ✓ |

\* Phase 2 may produce a non-identical TLV if `prettify_init` is called for the first time. Document the delta explicitly.

### Baseline Capture Procedure

Before Phase 1 begins:

```
make TARGET=host
./build/host/dat_to_tlv.exe <Games DAT args> -o baseline.tlv
```

Record file size and, if `crc32` or equivalent is available, a checksum. Store in `notes/refactor_baselines.txt` or equivalent.

---

## 10. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| A function marked file-local is actually called via a macro expansion that text search missed | Low | Medium — link error | Treat any link error after applying `static` as a signal; revert the specific function and investigate |
| Staged filter code calls a dead function removed from the header | Medium | Low — staged code does not compile in default build anyway | Search staged files before removing any declaration; add comment if hit |
| Read-back TLV path is needed for filter loading and gets accidentally removed | Medium | High — runtime data loss | Defer all read-back changes to Phase 2; make the decision explicit and recorded |
| `prettify_init` fix changes output titles; caller did not expect it | High | Medium — output delta is expected but must be reviewed | Run before/after comparison; do not commit without reviewing the delta |
| A rename misses one header and breaks the Amiga build (different include path) | Medium | Low — build error, not runtime | Build both targets after every individual rename before committing |
| CRC rename (`crc32_init` → `whdtlv_crc32_init`) misses an include and produces a link error | Medium | Low — caught at link time | Follow rename procedure §6.4 precisely; build after each rename |
| Facade wraps session loop but subtly changes parameter order or error handling | Low | High — silent output change | Facade implementation must be reviewed against `main.c` session loop line by line |
| Old harnesses in `tools/_legacy/` or `tests/_legacy/` include renamed headers and break | Low | Low — these are not in the default build | Document that legacy build breakage is acceptable; do not fix in this refactor |
| Phase 4 implementation reveals that `TLV_Record` must be part of the public API after all | Low | Medium — reopens the opaque-struct question | If unavoidable, typedef an opaque handle in the facade; do not expose struct internals |

---

## 11. Points Requiring Verification Before Implementation

The symbol inventory was produced by text search, not a compiler-assisted call graph. The following items are marked uncertain and must be verified before the implementing agent acts on them:

1. **`tlv_read_metadata_map` call chain.** The inventory records it as called only from `tlv_read_record_with_metadata`. Verify by searching for `tlv_read_metadata_map` across all 15 live source files. If any other caller exists, it cannot be made static in Phase 1.

2. **`csv_cache_add_unknown_token` vs `csv_cache_add_unknown_token_ex`.** The inventory records `csv_cache_add_unknown_token` as only called from `csv_cache_add_unknown_token_ex` within the same file. Confirm there is no external caller. The names are similar enough that a search hit on one might be confused with the other.

3. **`tlv_record_get_entry` and function pointers.** The inventory notes no function-pointer dispatch was observed. Confirm by searching for `(*` and `= tlv_record_get_entry` across all files before applying `static`.

4. **`prettify_init` intended call site.** The inventory does not confirm where `prettify_init` should be called. The implementing agent must read `src/utils/prettify.c` to understand what argument it expects and which caller owns that information.

5. **`field_registry_get_prescan_config` vs `field_registry_list_prescan_fields`.** The inventory states these are separate functions and that `get_prescan_config` is dead. Confirm they are not aliases or wrappers for each other before removing the declaration.

6. **`csv_cache_load_special_csv` and `csv_cache_update_special_csv`.** These are marked dead in the live build but may be intended for the staged filter modules. Search `include_raw/filter/` and `include_raw/filtering/` for references before removing declarations.

7. **`get_pack_type_by_id` location.** The inventory states it is defined at the end of `src_raw/filename_processor.c` and has no callers. Confirm its definition is truly in `filename_processor.c` and not in `pack_types_loader.c` before applying `static` or removing the header declaration.

8. **Amiga `whd_*` symbol deduplication.** Appendix B of the inventory notes that `whd_*` names generate real Amiga linker symbols. Before Phase 3, confirm whether WHDFetch uses the same `whd_*` implementations. If they are identical, no rename is needed. If they differ, a rename or link-time deduplication strategy is required before embedding.

---

## 12. Recommended Implementation Order — Coding Agent Prompts

The following prompts are intended to be issued one at a time to a coding agent. Do not issue Prompt N+1 until Prompt N has completed, the build passes, and the output TLV has been verified.

---

**Prompt 1 — Static-only cleanup**

> Implement Phase 1 of `docs/refactor_plan_whdtlv_namespace.md`.
> Make the functions listed in §4.1 `static` in their respective `.c` files.
> Remove their declarations from the listed headers.
> Verify each item in §4.2 before committing.
> Do not rename anything. Do not change Makefiles.

---

**Prompt 2 — Header hygiene and prettify_init decision**

> Implement Phase 2 of `docs/refactor_plan_whdtlv_namespace.md`.
> Fix the `prettify_init` gap per §5.1, resolve the dual-declaration per §5.2, remove dead header declarations per §5.6, and gate legacy TLV builder functions per §5.4.
> For the TLV read-back path (§5.5): read `src_raw/tlv_builder.c` and `notes/backport_inventory.md`, then decide and document which option (retain or guard) is correct.
> Verify all items in §5.7 before committing.

---

**Prompt 3 — Collision-risk renames**

> Implement Phase 3 of `docs/refactor_plan_whdtlv_namespace.md`.
> Apply renames in the priority order listed in §6.1.
> For each rename, follow the procedure in §6.4 exactly: search, list hits, apply, build both targets, commit.
> Do not batch multiple renames into a single commit.
> Verify §6.5 at the end.

---

**Prompt 4 — Create public facade**

> Implement Phase 4 of `docs/refactor_plan_whdtlv_namespace.md`.
> Create `include/integration/whdtlv_integration.h` and `src_raw/whdtlv_integration.c` as specified in §7.
> Add `whdtlv_integration.c` to the `SRC` list in the Makefile.
> Do not simplify `app_src/main.c` in this pass.
> Verify §7.7 before committing.

---

**Prompt 5 — Create demo harness**

> Implement Phase 5 of `docs/refactor_plan_whdtlv_namespace.md`.
> Create `tools/tlv_demo/tlv_demo.c` as specified in §8.
> Add a `make demo` target to the Makefile that builds it without modifying the default target.
> The demo must include only `<integration/whdtlv_integration.h>`.
> Verify §8.4 before committing.

---

**Prompt 6 — Update documentation**

> Update the following documents to reflect the completed refactor:
>
> - `docs/symbol_inventory.md`: add a note at the top that Phase 1–5 of the namespace refactor is complete and point to this plan.
> - `README.md`: update the public include example from internal headers to `#include <integration/whdtlv_integration.h>`.
> - `TLV_INTEGRATION_GUIDE.md`: update the API usage examples to use the new facade.
> - `notes/backport_inventory.md`: record the TLV read-back decision made in Phase 2.
>
> Do not rewrite any section that describes pipeline behaviour — only update the API surface and symbol references.

---

## Appendix — Symbol Inventory Cross-Reference

| Inventory section | Refactor plan section |
|------------------|-----------------------|
| §3 Public facade candidates | §3 Class A, §7 Phase 4 |
| §4 Internal shared subsystem | §3 Class C |
| §5 File-local helper candidates | §3 Class D, §4 Phase 1 |
| §6 Collision-risk symbols | §6 Phase 3 |
| §7 Header exposure concerns | §5.3, §5.4, §7 |
| §8 Dead or uncalled functions | §5.4, §5.5, §5.6 |
| §9 Legacy / test excluded | §2 Non-goals |
| §10 Phased plan (inventory) | Superseded by this document |
| Appendix B (`whd_*` symbols) | §11 item 8 |
