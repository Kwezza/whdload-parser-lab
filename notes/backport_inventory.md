# Backport Inventory - Milestone 0

Date: 2026-04-28
Scope: Extraction and classification only (no WHDFetch integration, no behavior changes)
Canonical asset source: assets/amiga_sys

## Manifest Summary

- defs CSV files: 19
- prefs INI files: 2
- profile files: 4
- source files (.c): 13
- header files (.h): 15
- total staged files: 53

## Use In Milestone 1

Rationale: These files are needed to parse filename tokens and run profile-based scoring for chipset/language/video/memory/media in the harness and later WHDFetch integration.

Assets and profiles:
- assets_raw/defs/Chipset.csv
- assets_raw/defs/Language.csv
- assets_raw/defs/Memory.csv
- assets_raw/defs/Video.csv
- assets_raw/defs/Media.csv
- assets_raw/defs/variant_tags.csv
- assets_raw/prefs/pack_types.ini
- assets_raw/profiles/chipset_aga_only.profile
- assets_raw/profiles/chipset_legacy_only.profile
- assets_raw/profiles/pal_aga_4mb.profile
- assets_raw/profiles/pal_aga_4mb_test.profile

Source modules:
- src_raw/filename_processor.c
- src_raw/field_registry.c
- src_raw/csv_cache.c
- src_raw/tlv_builder.c
- src_raw/error_handling.c
- src_raw/slug_util.c
- src_raw/filter_runtime.c
- src_raw/profile_loader.c
- src_raw/filter_profile.c
- src_raw/filter_pipeline.c
- src_raw/variant_iterator.c
- src_raw/variant_index.c
- src_raw/active_set.c

Headers:
- include_raw/tlv_filename/tlv_builder.h
- include_raw/tlv_filename/filename_processor.h
- include_raw/tlv_filename/field_registry.h
- include_raw/tlv_filename/csv_cache.h
- include_raw/tlv_filename/error_handling.h
- include_raw/tlv_filename/embedded_metadata.h
- include_raw/tlv_filename/slug_util.h
- include_raw/filter/filter_runtime.h
- include_raw/filter/filter_profile.h
- include_raw/filter/filter_pipeline.h
- include_raw/filter/variant_iterator.h
- include_raw/filter/variant_index.h
- include_raw/filter/active_set.h
- include_raw/filter/profile_loader.h
- include_raw/io/pack_types_loader.h

## Use In Milestone 2

Rationale: Special tags should be parsed and reported first; weighted scoring use is delayed.

- assets_raw/defs/Special.csv

## Reference Only

Rationale: Useful for catalog or enrichment context, but not required for Milestone 1 selector behavior.

- assets_raw/defs/compilations.csv
- assets_raw/defs/contributors.csv
- assets_raw/defs/cover_disks.csv
- assets_raw/defs/crack_groups.csv
- assets_raw/defs/demo_groups.csv
- assets_raw/defs/Disks.csv
- assets_raw/defs/filename.csv
- assets_raw/defs/magazines.csv
- assets_raw/defs/name_overrides.csv
- assets_raw/defs/official_amiga_bundles.csv
- assets_raw/defs/software_houses.csv
- assets_raw/defs/special_overrides.csv

## Ignore For WHDFetch

Rationale: No staged file is currently marked as hard-ignore. Keep this section for future triage.

- none

## Needs Review

Rationale: Needed for configuration context, but key naming and migration behavior must be standardized before integration.

- assets_raw/prefs/prefs.ini

## Known Gotchas

- Memory tokens are not simple numeric sizes; token semantics come from CSV mappings.
- Multiple spellings can map to the same concept.
- CSV IDs may intentionally be shared by aliases.
- Special tags are a mixed bucket and must not be fully scored in Milestone 1.
- Existing key mismatch exists: ActiveProfile versus active_profile. Standardize on active_profile in WHDFetch.
- Language tokens may be compact multi-language forms such as EnDe or FrDeEn.
- CSV cache has static/session state; initialization order matters.
- Field registry IDs are assigned at runtime and are not stable across sessions.
- Data files are runtime dependencies; extraction is not code-only.

## Canonical Source and Duplicate Notes

- Canonical source for staged profiles is assets/amiga_sys/profiles.
- Root duplicate profile intentionally not staged as canonical source:
  - profiles/pal_aga_4mb.profile

## Validation Checklist

- Staging tree created under variant_backport_staging.
- special copy.csv was renamed in staging only to Special.csv.
- Source still contains assets/amiga_sys/defs/special copy.csv.
- Staged source includes 10 required files plus 3 support dependencies.
- Every staged file is classified in exactly one bucket above.
