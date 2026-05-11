# Symbol Inventory — dat_to_tlv Live Build

**Date:** 2026-05-11  
**Branch:** main  
**Scope:** Host and Amiga production build as defined by the current `Makefile` `SRC` list.  
**Purpose:** Pre-refactor inventory. No functions are renamed, made static, or otherwise changed in this pass.

> **Refactor status (2026-05-11):** Phases 1–5 of the namespace refactor are complete.
> See `docs/refactor_plan_whdtlv_namespace.md` for the full plan and rationale.
> The current public API is exposed through `include/integration/whdtlv_integration.h`.
> Internal helper visibility, collision-risk renames, facade creation, and the demo
> harness (`tools/tlv_demo/tlv_demo.c`) have all been applied.

---

## 1. Executive Summary

The live build compiles 15 source files.  
All non-static functions across those files were catalogued (86 functions total).

Key findings:

- **24 functions are dead** (defined globally but have zero callers anywhere in the live build).  
  Several of these are declared in public headers and will become unnecessary linker symbols when this code is embedded in WHDFetch.
- **11 functions are only called within their own translation unit** and should become `static`.  
  Making them static is safe and does not require renaming.
- **4 generic platform utility symbols are extremely collision-prone**: `append_to_log`, `initialize_logfile`, `crc32_init`/`update`/`finalize`, and `validate_and_split`.
- **`prettify_init` is never called** but `prettify_title` and `prettify_shutdown` are — probable latent bug where the name-override CSV is silently never loaded.
- The **read-back TLV path** (`tlv_read_*`, `tlv_has_metadata_map`, `free_csv_fingerprint_map`) is fully implemented but has no callers; it is dead weight in the current build.
- `build_tlv_from_dat` and `tlv_builder_create_record` are labeled "legacy" in the header but are never called and can be removed or guarded from the public header.
- The session API (`tlv_session_init`, `tlv_session_process_batch`, `tlv_session_inject_group_ids`, `tlv_session_finalize`, `tlv_write_record_with_metadata`) is the natural public facade. It should be the only surface exposed through a future `whdtlv_integration.h`.

**Build status:** `gcc -std=c99 -Wall -Wextra` — **PASS, zero warnings, link OK.**  
Command used: `gcc -DPLATFORM_HOST=1 -DHOSTBUILD -std=c99 -O2 -Wall -Wextra -Iinclude -Iinclude/platform -Iinclude_raw -Iapp_src <all SRC files>`

---

## 2. Source Files Included in Live Build

| # | File | Layer | Role |
|---|------|-------|------|
| 1 | `app_src/main.c` | Harness | Entry point; orchestrates init, per-DAT loop, output, summary |
| 2 | `app_src/dat_parser_minimal.c` | Harness | DAT XML parser; extracts `<rom name=…>` and size/CRC attributes |
| 3 | `src_raw/error_handling.c` | Pipeline | Error context struct management |
| 4 | `src_raw/field_registry.c` | Pipeline | Runtime field-ID assignment from `pack_types.ini` |
| 5 | `src_raw/tlv_profile.c` | Pipeline | Compile-time-optional microsecond profiling |
| 6 | `src_raw/csv_cache.c` | Pipeline | CSV loading, hash-table lookup, unknown-token tracking |
| 7 | `src_raw/filename_processor.c` | Pipeline | Filename sanitise → prescan → tokenise → CSV-match orchestration |
| 8 | `src_raw/tlv_builder.c` | Pipeline | TLV record construction, session management, file I/O |
| 9 | `src_raw/group_util.c` | Pipeline | Canonical group-name derivation (strips version suffix) |
| 10 | `src/platform/platform_io.c` | Support | Platform I/O: `whd_readdir`, `whd_normalize_path` (Amiga only for most) |
| 11 | `src/platform/platform_string.c` | Support | `whd_strcasecmp` for vbcc Amiga; macro on host |
| 12 | `src/io/writeLog.c` | Support | Simple logfile append with format strings |
| 13 | `src/io/pack_types_loader.c` | Support | `pack_types.ini` parser; owns `PackType` allocation |
| 14 | `src/utils/prettify.c` | Support | WHDLoad name beautification with CSV override lookup |
| 15 | `src/utils/crc32.c` | Support | Table-driven CRC-32/ISO-HDLC |

**Not in build (staged/filter work):** `src_raw/active_set.c`, `src_raw/filter_pipeline.c`, `src_raw/filter_profile.c`, `src_raw/filter_runtime.c`, `src_raw/profile_loader.c`, `src_raw/slug_util.c`, `src_raw/variant_index.c`, `src_raw/variant_iterator.c`.

---

## 3. Public Facade Candidates

These are functions that a normal caller (WHDFetch or another embedder) would need.  
They should be declared in the future `include/integration/whdtlv_integration.h`.

| Function | Defined in | Current header | Callers (live) | Collision risk | Recommended action |
|----------|-----------|---------------|----------------|----------------|--------------------|
| `tlv_session_init` | `tlv_builder.c` | `tlv_builder.h` | `main.c` | Low | Rename `whdtlv_session_init`; move to facade header |
| `tlv_session_process_batch` | `tlv_builder.c` | `tlv_builder.h` | `main.c` | Low | Rename `whdtlv_session_process_batch` |
| `tlv_session_inject_group_ids` | `tlv_builder.c` | `tlv_builder.h` | `main.c` | Low | Rename `whdtlv_session_inject_group_ids` |
| `tlv_session_finalize` | `tlv_builder.c` | `tlv_builder.h` | `main.c` | Low | Rename `whdtlv_session_finalize` |
| `tlv_write_record_with_metadata` | `tlv_builder.c` | `tlv_builder.h` | `main.c` | Low–Medium | Rename `whdtlv_write_record`; called after session |
| `parse_dat_entries_minimal` | `dat_parser_minimal.c` | `dat_parser_minimal.h` | `main.c` | Low | Keep name; move declaration to facade or keep in harness |
| `free_dat_entries_minimal` | `dat_parser_minimal.c` | `dat_parser_minimal.h` | `main.c` | Low | Pair with above |
| `tlv_record_init` | `tlv_builder.c` | `tlv_builder.h` | `main.c`, `tlv_builder.c` | Low | Rename `whdtlv_record_init` |
| `tlv_record_free` | `tlv_builder.c` | `tlv_builder.h` | `main.c`, `tlv_builder.c` | Low | Rename `whdtlv_record_free` |
| `tlv_record_add_entry` | `tlv_builder.c` | `tlv_builder.h` | `main.c`, `filename_processor.c`, `tlv_builder.c` | Low | Rename `whdtlv_record_add_entry` |

---

## 4. Internal Shared Subsystem Functions

These cross module boundaries within the pipeline but should not be visible to external callers.  
They should be declared in internal headers only, not in a facade header.

### 4a. Field Registry

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `field_registry_alloc` | `field_registry.c` | `field_registry.h` | `main.c`, `tlv_builder.c` | Internal session setup only |
| `field_registry_free` | `field_registry.c` | `field_registry.h` | `main.c`, `tlv_builder.c` | Paired with alloc |
| `field_registry_get_id` | `field_registry.c` | `field_registry.h` | `csv_cache.c`, `filename_processor.c`, `tlv_builder.c` | Core lookup; hot path |
| `field_registry_get_name` | `field_registry.c` | `field_registry.h` | `field_registry.c`, `tlv_builder.c` | Used for metadata map write |
| `get_csv_filename_for_field` | `field_registry.c` | `field_registry.h` | `field_registry.c`, `filename_processor.c`, `tlv_builder.c` | **Generic name; collision risk. Rename `field_registry_get_csv_basename`** |
| `field_registry_get_count` | `field_registry.c` | `field_registry.h` | Not verified — presumed internal | Utility |
| `field_registry_get_allow_multiple` | `field_registry.c` | `field_registry.h` | `filename_processor.c` | Prescan control |
| `field_registry_set_allow_multiple` | `field_registry.c` | `field_registry.h` | `field_registry.c` (from `build_field_registry_from_ini`) | Config-time only |
| `field_registry_list_prescan_fields` | `field_registry.c` | `field_registry.h` | `filename_processor.c` | Prescan loop setup |
| `field_registry_add_field` | `field_registry.c` | `field_registry.h` | `tlv_builder.c` | Used during metadata map read-back |
| `build_field_registry_from_ini` | `field_registry.c` | `field_registry.h` | `main.c` | Config-time bootstrap |
| `build_field_registry_from_pack_types` | `field_registry.c` | `field_registry.h` | None — **dead** | See §8 |

### 4b. CSV Cache

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `csv_cache_manager_init` | `csv_cache.c` | `csv_cache.h` | `tlv_builder.c` | Session init; wraps `_with_config` |
| `csv_cache_manager_cleanup` | `csv_cache.c` | `csv_cache.h` | `tlv_builder.c` | Session finalize |
| `csv_cache_load_file` | `csv_cache.c` | `csv_cache.h` | `tlv_builder.c` | Triggered during session init |
| `csv_cache_lookup` | `csv_cache.c` | `csv_cache.h` | `filename_processor.c` | Hot path token lookup |
| `csv_cache_lookup_loaded` | `csv_cache.c` | `csv_cache.h` | `csv_cache.c`, `filename_processor.c` | Bypasses manager; direct cache lookup |
| `csv_cache_lookup_prehashed` | `csv_cache.c` | `csv_cache.h` | `csv_cache.c`, `filename_processor.c` | Optimised lookup with pre-computed hash |
| `csv_cache_lookup_span` | `csv_cache.c` | `csv_cache.h` | `csv_cache.c`, `filename_processor.c` | Multi-token window lookup |
| `csv_cache_find_token_source` | `csv_cache.c` | `csv_cache.h` | `csv_cache.c`, `filename_processor.c` | Reverse CSV identification |
| `csv_cache_add_unknown_token_ex` | `csv_cache.c` | `csv_cache.h` | `csv_cache.c`, `filename_processor.c` | Tracks unmatched tokens |
| `csv_cache_get_default_token` | `csv_cache.c` | `csv_cache.h` | `filename_processor.c` | Default-value fallback |
| `csv_cache_print_stats` | `csv_cache.c` | `csv_cache.h` | `main.c` | Profiling output; PROFILE=1 only |

### 4c. Filename Processor

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `tlv_process_filename_orchestrator` | `filename_processor.c` | `filename_processor.h` | `tlv_builder.c` | Main processing entry point per filename |
| `filename_sanitizer_process` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Strips extension, normalises |
| `version_parser_detect_pattern` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Detects `_v1.0` etc. |
| `version_parser_extract` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Extracts clean version string |
| `language_parser_parse_token` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Parses language bitfields |
| `contributor_extractor_process` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Multi-token contributor prescan |
| `csv_token_matcher_lookup` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Matches token against named CSV |
| `csv_token_matcher_find_source` | `filename_processor.c` | `filename_processor.h` | `filename_processor.c` (internal) | Finds which CSV owns token |
| `filename_processor_print_pack_field_stats` | `filename_processor.c` | `filename_processor.h` | `main.c` | Profiling |

> **Note:** All individual processing steps (`filename_sanitizer_process`, `version_parser_*`, etc.) are declared in the public `filename_processor.h` but only called from within `filename_processor.c`. They should be made `static` and removed from the header. Only `tlv_process_filename_orchestrator` and `filename_processor_print_pack_field_stats` need to remain visible across translation units.

### 4d. TLV Builder (internal helpers)

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `tlv_write_metadata_map` | `tlv_builder.c` | `tlv_builder.h` | `tlv_builder.c` (from `tlv_write_record_with_metadata`) | Should be static |
| `tlv_write_csv_fingerprints` | `tlv_builder.c` | `tlv_builder.h` | `tlv_builder.c` (from `tlv_write_record_with_metadata`) | Should be static |
| `tlv_write_group_map` | `tlv_builder.c` | `tlv_builder.h` | `tlv_builder.c` (from `tlv_write_record_with_metadata`) | Should be static |
| `tlv_record_add_field_by_name` | `tlv_builder.c` | `tlv_builder.h` | `tlv_builder.c` only | Should be static |
| `tlv_record_get_entry` | `tlv_builder.c` | `tlv_builder.h` | `tlv_builder.c` only | Should be static |

### 4e. Error Handling

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `processing_error_init` | `error_handling.c` | `error_handling.h` | `filename_processor.c`, `tlv_builder.c` | Shared; keep internal |
| `processing_error_set` | `error_handling.c` | `error_handling.h` | `filename_processor.c` | Shared; keep internal |
| `processing_error_is_set` | `error_handling.c` | `error_handling.h` | `filename_processor.c`, `tlv_builder.c` | Shared; keep internal |
| `processing_error_clear` | `error_handling.c` | `error_handling.h` | `filename_processor.c` | Shared; keep internal |

### 4f. Pack Types

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `load_pack_types` | `pack_types_loader.c` | `pack_types_loader.h`, `filename_processor.h` | `main.c`, `filename_processor.c`, `tlv_builder.c` | **Declared in two headers** — mismatch |
| `free_pack_types` | `pack_types_loader.c` | `pack_types_loader.h`, `filename_processor.h` | `main.c`, `filename_processor.c` | Same dual-declaration issue |

### 4g. Profiling

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `tlv_profile_reset` | `tlv_profile.c` | `tlv_profile.h` | `main.c` | Conditional on `TLV_PROFILE_ENABLE` |
| `tlv_profile_section_start` | `tlv_profile.c` | `tlv_profile.h` | `tlv_builder.c`, `filename_processor.c` | Macro wrapper in header |
| `tlv_profile_section_stop` | `tlv_profile.c` | `tlv_profile.h` | `tlv_builder.c`, `filename_processor.c` | Macro wrapper in header |
| `tlv_profile_print_summary` | `tlv_profile.c` | `tlv_profile.h` | `main.c` | Called on both stdout and summary file |
| `tlv_profile_log_summary` | `tlv_profile.c` | `tlv_profile.h` | Not verified (called internally in tlv_builder?) | Low priority |
| `tlv_profile_is_enabled` | `tlv_profile.c` | `tlv_profile.h` | `main.c` | Query function |

### 4h. Support (cross-module)

| Function | Defined in | Header | Callers | Notes |
|----------|-----------|--------|---------|-------|
| `prettify_title` | `prettify.c` | `prettify.h` | `csv_cache.c`, `filename_processor.c` | Used without init — see §8 |
| `prettify_shutdown` | `prettify.c` | `prettify.h` | `main.c` | Cleanup |
| `derive_group_name` | `group_util.c` | `group_util.h` | `tlv_builder.c` | Internal pipeline |
| `crc32_init` | `crc32.c` | `crc32.h` | `csv_cache.c` | Generic name — collision risk |
| `crc32_update` | `crc32.c` | `crc32.h` | `csv_cache.c` | Generic name — collision risk |
| `crc32_finalize` | `crc32.c` | `crc32.h` | `csv_cache.c` | Generic name — collision risk |
| `append_to_log` | `writeLog.c` | `writeLog.h` | Widespread (`main.c`, `csv_cache.c`, `tlv_builder.c`, `tlv_profile.c`) | **Highest collision risk of all symbols** |
| `set_logging_enabled` | `writeLog.c` | `writeLog.h` | `main.c` | Generic |
| `is_logging_enabled` | `writeLog.c` | `writeLog.h` | `tlv_profile.c` | Generic |
| `initialize_logfile` | `writeLog.c` | `writeLog.h` | `main.c` | Generic |

---

## 5. File-Local Helper Candidates

These are currently `extern` (non-static) but are only ever called within their own translation unit. They should be made `static` in the next pass.

| Function | Source file | Current visibility | Why it should be static |
|----------|------------|-------------------|------------------------|
| `validate_and_split` | `pack_types_loader.c` | extern | Only called from `load_pack_types` within the same file |
| `csv_direct_file_lookup` | `csv_cache.c` | extern | Only called from `csv_token_exists_in_any_csv` (file-local static) |
| `csv_cache_manager_init_with_config` | `csv_cache.c` | extern | Only called from `csv_cache_manager_init` in same file |
| `csv_cache_is_token_in_special` | `csv_cache.c` | extern | Only called from `csv_cache_add_unknown_token_ex` (line 1497) |
| `csv_cache_add_unknown_token` | `csv_cache.c` | extern | Only called from `csv_cache_add_unknown_token_ex` in same file |
| `tlv_record_get_entry` | `tlv_builder.c` | extern | Only called from `tlv_record_get_field_by_name` and `tlv_session_inject_group_ids` within `tlv_builder.c` |
| `tlv_record_add_field_by_name` | `tlv_builder.c` | extern | Only called from within `tlv_builder.c` |
| `tlv_write_metadata_map` | `tlv_builder.c` | extern | Only called from `tlv_write_record_with_metadata` |
| `tlv_write_csv_fingerprints` | `tlv_builder.c` | extern | Only called from `tlv_write_record_with_metadata` |
| `tlv_write_group_map` | `tlv_builder.c` | extern | Only called from `tlv_write_record_with_metadata` |
| `tlv_read_metadata_map` | `tlv_builder.c` | extern | Only called from `tlv_read_record_with_metadata` (which is itself dead) |
| `filename_sanitizer_process` | `filename_processor.c` | extern | Only called from within `filename_processor.c` |
| `version_parser_detect_pattern` | `filename_processor.c` | extern | Only called internally |
| `version_parser_extract` | `filename_processor.c` | extern | Only called internally |
| `language_parser_parse_token` | `filename_processor.c` | extern | Only called internally |
| `contributor_extractor_process` | `filename_processor.c` | extern | Only called internally |
| `csv_token_matcher_lookup` | `filename_processor.c` | extern | Only called internally |
| `csv_token_matcher_find_source` | `filename_processor.c` | extern | Only called internally |

> **Caveat:** Call graph was derived by text search. If any of these are referenced by a macro expansion or a function pointer, the analysis may be incorrect. Verify with a linker symbol report or `nm` before mechanically applying `static`.

---

## 6. Generic / Collision-Risk Symbols

Symbols that are likely to collide with WHDFetch or any other C library that gets linked alongside this module.

| Symbol | Type | Risk level | Reason | Recommended rename |
|--------|------|-----------|--------|-------------------|
| `append_to_log` | function | **Critical** | Completely generic log-append name; will collide with any logging system | `whdtlv_log` or `whdtlv_append_log` |
| `initialize_logfile` | function | **High** | Generic "init log" name | `whdtlv_log_init` |
| `set_logging_enabled` | function | **High** | Generic boolean setter | `whdtlv_log_set_enabled` |
| `is_logging_enabled` | function | **High** | Generic boolean query | `whdtlv_log_is_enabled` |
| `crc32_init` | function | **High** | Extremely common CRC function name; every embedded project has one | `whdtlv_crc32_init` or prefix with `dat2tlv_` |
| `crc32_update` | function | **High** | Same | `whdtlv_crc32_update` |
| `crc32_finalize` | function | **High** | Same | `whdtlv_crc32_finalize` |
| `validate_and_split` | function | **High** | Completely generic string-splitting helper name | Make `static` (see §5) |
| `prettify_init` | function | Medium | "init" + library-specific noun; lower risk than above, but unprefixed | `whdtlv_prettify_init` |
| `prettify_title` | function | Medium | Same | `whdtlv_prettify_title` |
| `prettify_shutdown` | function | Medium | Same | `whdtlv_prettify_shutdown` |
| `get_csv_filename_for_field` | function | Medium | Verb-first, no module prefix | `field_registry_get_csv_basename` |
| `load_pack_types` | function | Medium | Generic "load something" name | `whdtlv_load_pack_types` or make internal static |
| `free_pack_types` | function | Medium | Generic "free something" name | Pair with above |
| `derive_group_name` | function | Low–Medium | Somewhat domain-specific but unprefixed | `group_util_derive_name` or `whdtlv_derive_group_name` |
| `processing_error_init` | function | Low | Prefix `processing_error` is distinctive enough | Keep as-is, but behind internal header |
| `processing_error_set` | function | Low | Same | Keep as-is |
| `processing_error_is_set` | function | Low | Same | Keep as-is |
| `processing_error_clear` | function | Low | Same | Keep as-is |

---

## 7. Header Exposure Concerns

Headers that expose too much internals for a future embedded / caller-facing module:

| Header | Problem | Recommendation |
|--------|---------|---------------|
| `include_raw/tlv_filename/csv_cache.h` | Exposes full `GlobalCSVManager`, `CSVCache` struct internals, all 18 CSV functions including internal ones | Split: create an opaque `whdtlv_csv.h` with only `csv_cache_manager_init`, `_cleanup`, `_load_file`; hide everything else behind internal header |
| `include_raw/tlv_filename/filename_processor.h` | Declares all individual processing steps (`filename_sanitizer_process`, `version_parser_*`, etc.) that are implementation details; also declares `load_pack_types` and `get_pack_type_by_id` which live in other source files | Remove sub-step declarations; move `load_pack_types` declaration to `pack_types_loader.h` only; make `get_pack_type_by_id` static |
| `include_raw/tlv_filename/field_registry.h` | Exposes `FieldDefinition` struct internals including all prescan fields | Create opaque accessor API; callers should not depend on `FieldDefinition` layout |
| `include_raw/tlv_filename/tlv_builder.h` | Exposes `TLV_Record` / `TLV_Entry` struct internals; declares dead legacy functions (`tlv_builder_create_record`, `build_tlv_from_dat`); declares read-back functions with no live callers | Remove or guard dead legacy declarations; consider opaque `TLV_Record` for external callers |
| `include/io/writeLog.h` | `append_to_log`, `initialize_logfile`, `set_logging_enabled`, `is_logging_enabled` are all globally visible with generic names | Rename with `whdtlv_` prefix; move to internal header; expose only a single `whdtlv_set_log_callback` to callers |
| `include/platform/platform_io.h` | Declares `whd_normalize_path` which has no callers in the live build; mixes Amiga-only prototypes with host macros | Remove dead declaration; document which symbols are Amiga-only vs. host |
| `include/utils/crc32.h` | Declares `crc32_init/update/finalize` with generic unprefixed names | Rename to `whdtlv_crc32_*` or prefix; not needed in any caller-facing header |
| `include/utils/prettify.h` | `prettify_init` is declared but never called in live build — probable bug | Fix the latent bug first; then move behind internal header |
| `include_raw/tlv_filename/embedded_metadata.h` | Not in live `SRC` list directly; declares `metadata_maps_compatible` which has no callers | Verify whether this header is included transitively; mark as staged |

---

## 8. Dead or Uncalled Functions in Live Build

These are non-static functions that have **zero callers** in the current live source set.

> **Important:** "No callers found" was determined by plain-text search across the 15 live source files. Functions called only via function pointers or indirect dispatch would appear dead here but are not. No function-pointer dispatch patterns were observed in this codebase.

| Function | Defined in | Declared in | Status | Notes |
|----------|-----------|------------|--------|-------|
| `parse_dat_filenames_minimal` | `dat_parser_minimal.c` | `dat_parser_minimal.h` | Dead | Superseded by `parse_dat_entries_minimal`; name-only API no longer used by main |
| `free_dat_filenames_minimal` | `dat_parser_minimal.c` | `dat_parser_minimal.h` | Dead | Pair of above |
| `build_tlv_from_dat` | `tlv_builder.c` | `tlv_builder.h` | Dead | Legacy all-in-one facade; `tlv_session_*` API used instead |
| `tlv_builder_create_record` | `tlv_builder.c` | `tlv_builder.h` | Dead | Labeled "legacy interface" in header; no callers |
| `tlv_record_get_field_by_name` | `tlv_builder.c` | `tlv_builder.h` | Dead | `tlv_record_get_entry` (by field ID) used instead; this name-based accessor has no callers |
| `tlv_read_record_with_metadata` | `tlv_builder.c` | `tlv_builder.h` | Dead | TLV read-back path not exercised in current build |
| `tlv_has_metadata_map` | `tlv_builder.c` | `tlv_builder.h` | Dead | Part of read-back path |
| `tlv_read_csv_fingerprints` | `tlv_builder.c` | `tlv_builder.h` | Dead | Part of read-back path |
| `free_csv_fingerprint_map` | `tlv_builder.c` | `tlv_builder.h` | Dead | Part of read-back path |
| `csv_cache_reverse_lookup` | `csv_cache.c` | `csv_cache.h` | Dead | No callers found anywhere in live build |
| `csv_cache_update_special_csv` | `csv_cache.c` | `csv_cache.h` | Dead | Staged for future special-token updating |
| `csv_cache_get_crc` | `csv_cache.c` | `csv_cache.h` | Dead | `tlv_write_csv_fingerprints` reads CRC directly from struct member `caches[i].crc32` |
| `csv_cache_get_memory_stats` | `csv_cache.c` | `csv_cache.h` | Dead | Neither `csv_cache_report_summary` nor `csv_cache_print_stats` calls it |
| `csv_cache_report_summary` | `csv_cache.c` | `csv_cache.h` | Dead | No callers; only Amiga `#if` content; `csv_cache_print_stats` is separate |
| `csv_cache_load_special_csv` | `csv_cache.c` | `csv_cache.h` | Dead | Staged for future use |
| `field_registry_validate` | `field_registry.c` | `field_registry.h` | Dead | Defensive validation utility; never called in pipeline |
| `field_registry_has_available_ids` | `field_registry.c` | `field_registry.h` | Dead | Capacity check; pipeline checks `field_count >= max_fields` directly |
| `field_registry_set_default` | `field_registry.c` | `field_registry.h` | Dead | Default token IDs written directly into struct during INI parse |
| `field_registry_get_default_token_id` | `field_registry.c` | `field_registry.h` | Dead | No callers |
| `field_registry_get_prescan_config` | `field_registry.c` | `field_registry.h` | Dead | `field_registry_list_prescan_fields` used instead |
| `build_field_registry_from_pack_types` | `field_registry.c` | `field_registry.h` | Dead | `build_field_registry_from_ini` used instead; pack-types array path unused |
| `get_pack_type_by_id` | `filename_processor.c` | `filename_processor.h` | Dead | Defined at end of file; not called by any other function in live build |
| `prettify_init` | `prettify.c` | `prettify.h` | Dead — **potential bug** | `prettify_title` and `prettify_shutdown` are called but `prettify_init` is never called; name-override CSV is never loaded |
| `whd_normalize_path` | `platform_io.c` | `platform_io.h` | Dead | Declared but never called in any live source file |

---

## 9. Legacy / Test / Demo-only Symbols Excluded from Refactor

The following are **not in the live Makefile build**. They should not drive the namespace refactor and are listed here for completeness.

| Symbol / File | Reason excluded |
|---------------|----------------|
| `src_raw/filter_pipeline.c` and headers | Staged filter work; not wired into Makefile |
| `src_raw/filter_profile.c` | Staged |
| `src_raw/filter_runtime.c` | Staged |
| `src_raw/active_set.c` | Staged |
| `src_raw/profile_loader.c` | Staged |
| `src_raw/slug_util.c` (`tlv_make_identity_slug`, `tlv_hash_identity_key`) | Not in SRC list; also uses `<io/status_store.h>` which does not exist in the repo |
| `src_raw/variant_index.c`, `src_raw/variant_iterator.c` | Staged |
| `tests/_legacy/`, `tools/_legacy/` | Legacy/removed from build |
| `include_raw/filter/`, `include_raw/filtering/` | Headers for staged filter work |
| `include_raw/group_util.h` `csv_cache.h` — staged accessors | Any function in these headers backed only by the staged files above |

---

## 10. Recommended Phased Refactor Plan

### Phase 1 — Safe static conversions (no renaming, no API change, lowest risk)

Apply `static` to all functions listed in §5.  
These functions are only used within their own `.c` file.  
This phase eliminates the most linker-symbol pollution with zero behavioral change.

**Priority order:**
1. `validate_and_split` in `pack_types_loader.c`
2. All eight internal processing steps in `filename_processor.c` (`filename_sanitizer_process`, `version_parser_*`, `language_parser_*`, `contributor_extractor_*`, `csv_token_matcher_*`)
3. Five helpers in `tlv_builder.c` (`tlv_write_metadata_map`, `tlv_write_csv_fingerprints`, `tlv_write_group_map`, `tlv_record_add_field_by_name`, `tlv_record_get_entry`)
4. Internal csv_cache helpers (`csv_direct_file_lookup`, `csv_cache_manager_init_with_config`, `csv_cache_is_token_in_special`, `csv_cache_add_unknown_token`)
5. `tlv_read_metadata_map` in `tlv_builder.c`

Remove the declarations of these functions from their headers in the same pass.

### Phase 2 — Fix latent bugs and remove dead code

1. **Fix `prettify_init` gap:** Determine where `prettify_init` should be called. Insert the call (likely in `tlv_session_init` or `main.c`) or remove the function if no CSV override is needed.
2. **Remove or gate dead legacy API:** Remove `tlv_builder_create_record` and `build_tlv_from_dat` definitions and their header declarations, or move them to a `#if WHDTLV_LEGACY_API` guard.
3. **Remove dead read-back path from headers:** Remove declarations of `tlv_read_record_with_metadata`, `tlv_has_metadata_map`, `tlv_read_csv_fingerprints`, `free_csv_fingerprint_map` from `tlv_builder.h` unless the read path is being actively developed.
4. **Remove or make static:** `parse_dat_filenames_minimal` / `free_dat_filenames_minimal`, `csv_cache_reverse_lookup`, `csv_cache_report_summary`, `csv_cache_get_memory_stats`, `csv_cache_get_crc`, `field_registry_validate`, `field_registry_has_available_ids`, `field_registry_set_default`, `field_registry_get_default_token_id`, `field_registry_get_prescan_config`, `build_field_registry_from_pack_types`, `get_pack_type_by_id`, `whd_normalize_path`.
5. **Resolve `load_pack_types` / `free_pack_types` dual-declaration:** Remove the copies from `filename_processor.h`; source of truth is `pack_types_loader.h` only.

### Phase 3 — Rename collision-risk symbols

Rename in this priority order (highest collision risk first):

1. `append_to_log` → `whdtlv_log` (or provide a callback hook)
2. `initialize_logfile` → `whdtlv_log_init`
3. `set_logging_enabled` / `is_logging_enabled` → `whdtlv_log_set_enabled` / `whdtlv_log_is_enabled`
4. `crc32_init` / `crc32_update` / `crc32_finalize` → `whdtlv_crc32_init` / `_update` / `_finalize`
5. `prettify_init` / `prettify_title` / `prettify_shutdown` → `whdtlv_prettify_*`
6. `get_csv_filename_for_field` → `field_registry_get_csv_basename`
7. `load_pack_types` / `free_pack_types` → either make static (Phase 1) or rename `whdtlv_load_pack_types`

### Phase 4 — Create public facade header

Create `include/integration/whdtlv_integration.h` exposing only:

```c
bool whdtlv_session_init(const char *csv_dir, const char *pack_types_ini);
bool whdtlv_session_process_batch(const char **filenames, uint32_t count,
                                  uint32_t pack_type_id, TLV_Record *out_records,
                                  ProcessingSummary *summary);
bool whdtlv_session_inject_group_ids(TLV_Record *records, uint32_t count);
bool whdtlv_write_record(FILE *file, const TLV_Record *record,
                         const FieldRegistry *registry);
void whdtlv_session_finalize(void);

/* Optional: if caller manages its own DAT parsing */
size_t whdtlv_parse_dat(const char *dat_path, DatRomEntry **out_entries);
void   whdtlv_free_dat(DatRomEntry *entries, size_t count);

/* Optional: record lifecycle if caller builds records manually */
bool whdtlv_record_init(TLV_Record *record);
void whdtlv_record_free(TLV_Record *record);
bool whdtlv_record_add_entry(TLV_Record *record, uint8_t field_id,
                             const uint8_t *value, uint16_t length);
```

Everything else stays behind internal headers and should not be `#include`d by callers.

---

## Appendix A — Build Verification

```
Command: gcc -DPLATFORM_HOST=1 -DHOSTBUILD -std=c99 -O2 -Wall -Wextra \
             -Iinclude -Iinclude/platform -Iinclude_raw -Iapp_src \
             <all 15 SRC files individually, then linked>
Result:  PASS — zero warnings, link succeeded.
Binary:  build/host/dat_to_tlv.exe
```

> Note: `make TARGET=host` invokes `help` as the default target (no `.DEFAULT_GOAL` or `all:` alias is set). Use `make $(BIN)` or invoke `gcc` directly to build without the help output.

---

## Appendix B — Collision Analysis for `whd_*` Platform Symbols

The platform abstraction layer uses the `whd_` prefix (`whd_malloc`, `whd_free`, `whd_fopen`, `whd_opendir`, `whd_readdir`, `whd_closedir`, `whd_mkdir`, `whd_access`, `whd_remove`, `whd_rename`, `whd_strcasecmp`, `whd_strtok_r`, `whd_normalize_path`).

- On **host builds**, most of these are `#define` macros aliasing POSIX/C-stdlib calls, so they create no linker symbols.
- On **Amiga builds**, `whd_readdir`, `whd_opendir`, `whd_closedir`, `whd_mkdir`, `whd_access`, `whd_remove`, `whd_rename`, and `whd_strcasecmp` generate real symbols.
- The `whd_` prefix is reasonably distinctive but does not follow the `whdtlv_` convention proposed for this module. If this library ships as a shared object alongside WHDFetch, there is a moderate collision risk on the Amiga where `whd_*` symbols from both would occupy the same namespace.
- Recommendation: document that all `whd_*` functions are shared infrastructure between WHDFetch and dat_to_tlv, and that no deduplication rename is needed if they are identical implementations.

---

*End of symbol inventory. This document should be updated after each refactor phase.*
