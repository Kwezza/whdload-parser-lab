# Documentation Suite Plan — dat_to_tlv

> **Agent instructions:** This file tracks a multi-session documentation effort.
> After completing each session, update the status table and the relevant session
> section (mark `[ ]` items as `[x]` and note any decisions or deviations).
> Do not remove completed sections — keep them as a record.

---

## Background & Purpose

`dat_to_tlv` is a tool that converts WHDLoad DAT catalogue files into a compact
binary TLV index designed to run on a real Amiga. A key design goal is that the
system behaves like a **framework controlled by configuration files** — CSV lookup
tables, an INI pack-type definition, and profile files — so that users can extend
or customise behaviour (add new tokens, new fields, new filter profiles) without
touching source code or recompiling.

This documentation effort creates two tiers of docs:

- **Executive overview** — for Amiga enthusiasts and WHDLoad users who want to
  understand what the tool does and how to configure it, without needing to read code.
- **Deep-dive technical docs** — for contributors or curious users who want to
  understand the internal architecture, the binary format, and every extension point.

Output folders:
- `docs/how-it-works/Overview/` — executive document (1 file)
- `docs/how-it-works/DeepDive/` — technical documents (3 files)

---

## Source Tree Reference (post namespace-refactor)

> **Note for agents:** `app_src/` and `src_raw/` no longer exist. The tree below
> is the current layout. Always verify against the `Makefile` SRC list before
> stating whether a module is active or staged.

| Folder | Purpose |
|--------|---------|
| `tools_src/dat_to_tlv_main.c` | Program entry point |
| `src/whdtlv/core/` | Active pipeline: DAT parse, field registry, CSV cache, filename processor, TLV builder, group util, variant iterator/index, active set |
| `src/whdtlv/filtering/` | Filtering subsystem: tlv_reader, tlv_runtime, tlv_filter, tlv_variant, tlv_group, tlv_select, tlv_results, tlv_crc_validate, profile_binder, selection_plan, profile_loader, filter_pipeline, filter_profile, filter_runtime, whd_search |
| `src/whdtlv/io/` | pack_types_loader, writeLog |
| `src/whdtlv/platform/` | platform_io, platform_string |
| `src/whdtlv/utils/` | prettify, crc32 |
| `src/whdtlv/whdtlv_filter_facade.c` | Public filter facade |
| `assets_raw/defs/` | CSV lookup tables (one per field) |
| `assets_raw/prefs/pack_types.ini` | Pack type & field definitions |
| `assets_raw/prefs/prefs.ini` | Runtime preferences |
| `assets_raw/profiles/` | Named filter profiles |

**Currently compiled (Makefile SRC):** `dat_to_tlv_main.c`, `dat_parser_minimal.c`,
`error_handling.c`, `field_registry.c`, `tlv_profile.c`, `csv_cache.c`,
`filename_processor.c`, `tlv_builder.c`, `group_util.c`, `whdtlv_integration.c`,
`platform_io.c`, `platform_string.c`, `writeLog.c`, `pack_types_loader.c`,
`prettify.c`, `crc32.c`, `profile_binder.c`, `selection_plan.c`,
`tlv_crc_validate.c`, `tlv_filter.c`, `tlv_group.c`, `tlv_reader.c`,
`tlv_results.c`, `tlv_runtime.c`, `tlv_select.c`, `tlv_variant.c`,
`whd_search.c`, `whdtlv_filter_facade.c`

**Present in repo but NOT in Makefile SRC (staged/legacy):**
`variant_iterator.c`, `variant_index.c`, `active_set.c`, `slug_util.c`,
`filter_profile.c`, `filter_pipeline.c`, `filter_runtime.c`, `profile_loader.c`

---

## Decisions Made

| Decision | Choice |
|----------|--------|
| Executive audience | Amiga enthusiasts / WHDLoad users (non-technical) |
| Staged modules | Include in deep-dive, clearly labelled as staged/not compiled |
| Deep-dive granularity | 3 documents (fewer, bigger) |
| Code reference style | File links + function names in prose; minimal code blocks |

---

## Progress Tracker

| Session | Document | Status |
|---------|----------|--------|
| 1 | `docs/how-it-works/Overview/executive-overview.md` | `[x] Complete` |
| 2 | `docs/how-it-works/DeepDive/01-architecture-and-creation-pipeline.md` | `[x] Complete` |
| 3 | `docs/how-it-works/DeepDive/02-filtering-system.md` | `[x] Complete` |
| 4 | `docs/how-it-works/DeepDive/03-extensibility-guide.md` | `[x] Complete` |

---

## Session 1 — Executive Overview

**Output:** `docs/how-it-works/Overview/executive-overview.md`
**Audience:** Amiga enthusiasts / WHDLoad users — no programming knowledge assumed.

### Content checklist

- [x] What WHDLoad is and why archive variants create a problem (OCS, AGA, PAL, French, etc.)
- [x] What `dat_to_tlv` does at a high level: reads a catalogue → builds a compact index → a profile picks the best variant for your machine
- [x] How the system is designed so users control it through text files — no compiler needed
- [x] What a user can actually change: tokens (CSV files), fields and pack types (INI), filter preferences (profile files)
- [x] Short glossary: TLV, DAT, token, field, pack type, profile

### Completion notes (2026-05-26)

- All checklist items covered.
- Glossary includes: TLV, DAT, token, field, pack type, profile, group, CRC-32.
- Chipset.csv and pal_aga_4mb.profile shown as inline illustrative snippets (not full code blocks).
- Links to `docs/pack-types-ini-format.md` and `docs/profile_system.md` rather than duplicating content.
- No content duplicated from `docs/tlv-pipeline-overview.md`.

### Key reference material

- `README.md`
- `assets_raw/prefs/pack_types.ini`
- `assets_raw/defs/Chipset.csv` (concrete example of a lookup table)
- `assets_raw/profiles/pal_aga_4mb.profile` (concrete example of a profile)
- `docs/tlv-pipeline-overview.md` (source material, do not duplicate)

---

## Session 2 — System Architecture & Creation Pipeline

**Output:** `docs/how-it-works/DeepDive/01-architecture-and-creation-pipeline.md`

### Content checklist

- [x] Full module map — all source folders, what lives where and why
- [x] Active pipeline data flow step by step:
  - `dat_to_tlv_main.c` → `dat_parser_minimal.c` → `pack_types_loader.c`
    → `field_registry.c` → `csv_cache.c` → `filename_processor.c`
    (prescan + tokenize loop) → `tlv_builder.c`
- [x] How field IDs are assigned dynamically at runtime from `pack_types.ini` — not hardcoded constants (`field_registry_alloc`, `field_registry_add_field_internal`)
- [x] TLV binary structure: header blocks (`0x01` field-map, `0x02` group-map, `0x04` CSV fingerprints), then data records
- [x] How `group_util.c` derives a canonical group name and how `group_id` entries act as variant grouping keys
- [x] How CRC-32 checksums (`crc32.c`) link the finished TLV to the exact CSV version that produced it
- [x] Key function names: `whdtlv_build_from_dat`, `tlv_process_filename_orchestrator`, `field_registry_alloc`, `prescan_and_strip_tokens`, `csv_cache_lookup_loaded`, `tlv_record_add_entry`
- [x] Staged/legacy modules in repo but not compiled — note clearly

### Completion notes (2026-05-26)

- All checklist items covered.
- Module map table covers all active `src/whdtlv/` subfolders plus `tools_src/` entry point.
- Staged modules (`variant_iterator.c`, `variant_index.c`, `active_set.c`, `slug_util.c`) called out explicitly.
- Block `0x03` (file version) documented alongside the other header blocks.
- Key function reference table lists 13 functions covering the full pipeline.
- `archive_info` 8-byte encoding (size-KiB BE + CRC-32 BE) documented in Stage 5.
- CSV alias-row rules and compact multilanguage token rule documented in their respective stages.
- Links to `docs/tlv-pipeline-overview.md`, `docs/pack-types-ini-format.md`, `docs/prerequisites.md`, and the next deep-dive doc.

### Key source files

- `tools_src/dat_to_tlv_main.c`
- `src/whdtlv/core/dat_parser_minimal.c`
- `src/whdtlv/io/pack_types_loader.c`
- `src/whdtlv/core/field_registry.c`
- `src/whdtlv/core/csv_cache.c`
- `src/whdtlv/core/filename_processor.c`
- `src/whdtlv/core/tlv_builder.c`
- `src/whdtlv/core/group_util.c`
- `src/whdtlv/utils/crc32.c`

### Reference material (do not duplicate)

- `docs/tlv-pipeline-overview.md`
- `docs/prerequisites.md` (DAT stem extraction rules)
- `docs/pack-types-ini-format.md`

---

## Session 3 — Filtering System

**Output:** `docs/how-it-works/DeepDive/02-filtering-system.md`

### Content checklist

- [x] Purpose: TLV is self-describing; filtering reads it without re-parsing DAT or CSVs
- [x] Stage 1 — Load & validate (`tlv_reader.c`): field-map `0x01`, group-map `0x02`, CRC fingerprints `0x04` (`tlv_crc_validate.c`), `data_offset` boundary
- [x] Stage 2 — Variant views (`tlv_variant.c`): how the runtime reconstructs per-variant data from the flat TLV stream; `tlv_group.c` for group-level operations
- [x] Stage 3 — Runtime initialisation (`tlv_runtime.c`): loads field registry from embedded map, links CSV names; `profile_binder.c` resolves `.profile` filter tokens to numeric IDs
- [x] Stage 4 — Selection plan (`selection_plan.c`): how include/exclude lists + weights produce a ranked result per group; FNV-1a hash fallback for tokens not in CSV
- [x] Stage 5 — Filter execution (`tlv_filter.c`, `tlv_select.c`): per-variant scoring, rejection, active variant set; `tlv_results.c` collects the final ranked list
- [x] Stage 6 — Search (`whd_search.c`): substring / prefix / multi-term search over variant display names
- [x] Public facade (`whdtlv_filter_facade.c`): the single entry point callers use
- [x] Profile file format (`.profile` INI): `[Profile]`, `[Filter.field]` with `include=`/`exclude=`, `[Scoring]` with `weight.field=N`
- [x] Token resolution at filter time: CSV lookup via `profile_binder.c` → FNV-1a 8-bit hash fallback
- [x] Built-in profiles: `pal_aga_4mb`, `chipset_aga_only`, `chipset_legacy_only`, `multi_bucket_reference`
- [x] Staged/legacy filtering modules NOT in Makefile: `filter_profile.c`, `filter_pipeline.c`, `filter_runtime.c`, `profile_loader.c`, `variant_iterator.c`, `variant_index.c`, `active_set.c` — note clearly

### Completion notes (2026-05-26)

- All checklist items covered.
- Six-stage pipeline table at document top gives a quick-reference map of every module.
- `data_offset` boundary, little-endian byte order, and block type bytes (`0x01`, `0x02`, `0x04`) documented in Stage 1.
- CRC strict vs warn-only modes documented; `crc_mismatch_count` surfacing noted.
- `WhdVariantView` field table documents `filename`, `base_name`, `group_id`, `original_index`.
- Two grouping paths (`group_id` vs `base_name` fallback) documented in Stage 2.
- FNV-1a 8-bit hash fallback and `had_warnings` flag documented in Stage 3.
- Slash-bucket mechanism explained with concrete `include=AGA/ECS,OCS` example in Stage 4.
- Multi-value field scoring and tie-break rules documented in Stage 5.
- Substring vs wildcard search modes documented for `whd_search.c`.
- Public facade function table and link to `docs/whdtlv_public_filter_facade.md`.
- Profile format summary with illustrative snippet; full format delegated to `docs/profile_system.md`.
- All five built-in profiles listed with descriptions.
- Seven staged/legacy modules called out with explicit status callout.
- Key function reference table covers 16 functions across all stages.

### Key source files

- `src/whdtlv/filtering/tlv_reader.c`
- `src/whdtlv/filtering/tlv_runtime.c`
- `src/whdtlv/filtering/tlv_variant.c`
- `src/whdtlv/filtering/tlv_group.c`
- `src/whdtlv/filtering/tlv_filter.c`
- `src/whdtlv/filtering/tlv_select.c`
- `src/whdtlv/filtering/tlv_results.c`
- `src/whdtlv/filtering/tlv_crc_validate.c`
- `src/whdtlv/filtering/profile_binder.c`
- `src/whdtlv/filtering/selection_plan.c`
- `src/whdtlv/filtering/whd_search.c`
- `src/whdtlv/whdtlv_filter_facade.c`
- `assets_raw/profiles/`

### Reference material (do not duplicate)

- `docs/tlv-filtering-overview.md`
- `docs/profile_system.md`

---

## Session 4 — Extensibility Guide

**Output:** `docs/how-it-works/DeepDive/03-extensibility-guide.md`

### Content checklist

- [ ] Core philosophy: this is a framework driven by config files, not hardcoded logic
- [ ] **Add a token to an existing field** — edit one CSV in `assets_raw/defs/` (e.g. new row in `Chipset.csv`); explain format `<id>,<token>,<description>[,default]`
- [ ] **Add a new field** — add name to `FieldList` in `pack_types.ini`; create a new CSV; optionally configure `[FieldAttributes]` (allow_multiple, prescan.enabled, prescan.order, prescan.remove_from_filename)
- [ ] **Add a new pack type** — new numbered entry in `pack_types.ini` with `DatName` stem, `DisplayName`, `FieldList`
- [ ] **Create or modify a filter profile** — new `.profile` file in `assets_raw/profiles/`; `[Profile]` metadata, `[Filter.field]` sections, `[Scoring]` weights
- [ ] **What happens when something is unknown at runtime** — unrecognised filter section silently skipped; token not in CSV hashed with FNV-1a (no crash, `had_warnings` set)
- [ ] **Limitations** — what genuinely requires recompilation: new prescan attribute types, new field attribute semantics beyond the current `[FieldAttributes]` set
- [ ] Worked example for each of the four extension types above

### Completion notes (2026-05-26)

- All checklist items covered.
- Four extension types presented in order of increasing scope with a summary table at the top.
- Worked example provided for each extension type: add a chipset token (`RTG`/`P96` alias), add a `controller` field, add a `Utils` pack type, create a `cd32_pal.profile`.
- Quick-reference table at document end maps user intentions to exact files to edit.
- Alias-row rules (same ID, multiple spellings) explained with canonical vs alias token distinction.
- `[FieldAttributes]` keys tabulated: `allow_multiple`, `prescan.enabled`, `prescan.order`, `prescan.remove_from_filename`, `prescan.multi_token`.
- "What happens when something is unknown" section covers: unrecognised filter section (silently skipped), token not in CSV (FNV-1a 8-bit hash fallback — **silent, had_warnings NOT set**), unrecognised attribute key (silently ignored). `had_warnings` is set only for unknown field names in `[Filter.<field>]`/`[Scoring]` and OOM in `ensure_bound_field()`.
- Limitations table lists the five categories that genuinely require recompilation.
- Links to `docs/pack-types-ini-format.md` and `docs/profile_system.md` rather than duplicating their content.
- `multi_bucket_reference.profile` referenced for slash-bucket worked example.
- Staged modules called out with explicit status note.

### Key reference files

- `assets_raw/prefs/pack_types.ini`
- `assets_raw/defs/Chipset.csv`
- `assets_raw/profiles/pal_aga_4mb.profile`
- `docs/pack-types-ini-format.md` (do not duplicate — link to it)
- `docs/profile_system.md` (do not duplicate — link to it)

---

## Style Rules for All Sessions

- File references must be Markdown links (`[path/file.c](path/file.c)`), not backticks.
- Function names in prose use backticks: `` `function_name()` ``.
- No large code blocks — small illustrative snippets only where essential.
- Staged/legacy modules must carry a visible callout, e.g.:
  > **Status: Staged — present in repo but not compiled by the current Makefile.**
- Do not duplicate content already in `docs/tlv-pipeline-overview.md`,
  `docs/tlv-filtering-overview.md`, `docs/pack-types-ini-format.md`, or
  `docs/profile_system.md` — link to them instead.
- After writing each document, verify all linked file paths resolve in the workspace.