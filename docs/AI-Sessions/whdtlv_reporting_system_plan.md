# WHDTLV Reporting System - Implementation Brief

## Purpose

Create a new host-side reporting/export subsystem for the WHDTLV project.

The goal is to make a prebuilt TLV file inspectable outside the filtering pipeline. The tool should read an existing `.tlv` file, decode its records, resolve stored numeric values back to human-readable CSV tokens/descriptions where possible, and write a CSV file suitable for opening in Excel.

This is primarily a debugging and visualisation feature. It should help explain how the TLV builder broke down filenames into fields, how variants were grouped, and where user-defined field/CSV configuration may be producing unexpected results.

The system should support both:

- **Wide mode**: one row per archive/variant, with one or more columns per field.
- **Long mode**: one row per stored field value, useful for deep debugging of multi-value fields.

The first implementation should be host-side only. It must not increase the Amiga WHDFetch runtime footprint unless explicitly enabled later.

---

## Background

The current WHDTLV project has these relevant characteristics:

- A TLV file is built on the host from a RetroPlay WHDLoad DAT file.
- The TLV contains a field map header block, so field IDs can be resolved to field names at runtime.
- The TLV may contain a group map header block mapping `group_id` values to canonical game names.
- Each variant starts with a `display_name` field.
- Each variant may carry many metadata fields such as chipset, language, memory, media, publisher, contributors, crack groups, etc.
- Some fields are CSV-backed token fields.
- Some fields are free-form string fields.
- Some fields may be multi-value, meaning the same field can appear multiple times in one variant.
- `group_id` is a special field used for grouping, not scoring.
- `archive_info` is a special field containing archive size and CRC data from the original DAT.
- The filtering system should remain lean and focused on selecting variants.

This reporting system should reuse existing TLV loading/parsing helpers where practical, but it must remain a separate subsystem from filtering.

---

## Recommended Folder Layout

Add a new reporting subsystem alongside the existing WHDTLV internal areas:

```text
src/whdtlv/
    core/
    filtering/
    io/
    platform/
    reporting/
        whdtlv_report_csv.c
        whdtlv_report_csv.h
        whdtlv_report_types.h      optional, only if useful
    utils/
```

Add a separate command-line front-end tool under the tools area:

```text
tools_src/whdtlv_report/
    main.c
```

Do not put the command-line executable inside `src/whdtlv/utils/`. The `utils` folder should remain for reusable helper code, not application entry points.

---

## Design Principle

Keep three layers separate:

```text
1. Core TLV reader helpers
   Reads field map, group map, variants, group_id, archive_info.

2. Filtering subsystem
   Uses the TLV data to apply profiles and select archive filenames.

3. Reporting subsystem
   Uses the TLV data to explain/export all stored metadata.
```

The reporting subsystem may share lower-level TLV parsing with the filter, but it should not depend on profile loading or scoring.

---

## Proposed Public/Internal API

The reporting system can initially be internal to the project rather than added to the main WHDFetch-facing public facade.

Suggested function:

```c
typedef enum WhdTlvReportCsvMode {
    WHDTLV_REPORT_CSV_WIDE = 0,
    WHDTLV_REPORT_CSV_LONG = 1
} WhdTlvReportCsvMode;

typedef struct WhdTlvReportOptions {
    WhdTlvReportCsvMode mode;
    int include_ids;
    int include_descriptions;
    int include_status;
    int only_multi_variant_groups;
    int only_problem_rows;
    unsigned int reserved[8];
} WhdTlvReportOptions;

typedef struct WhdTlvReportSummary {
    unsigned long variants_total;
    unsigned long groups_total;
    unsigned long rows_written;
    unsigned long fields_written;
    unsigned long values_resolved;
    unsigned long values_unresolved;
    unsigned long problem_rows;
    unsigned long multi_value_fields_seen;
    unsigned long multi_variant_groups_seen;
    unsigned int reserved[8];
} WhdTlvReportSummary;

void whdtlv_report_options_defaults(WhdTlvReportOptions *opts);

int whdtlv_report_csv_file(
    const char *tlv_path,
    const char *defs_dir,
    const char *output_csv_path,
    const WhdTlvReportOptions *options,
    WhdTlvReportSummary *summary
);
```

Naming may be adjusted to match the existing project conventions, but keep it clearly separate from filtering.

---

## Command-Line Tool

Create a host-side command-line wrapper:

```text
whdtlv_report --tlv output/Games.tlv --defs assets_raw/defs --out output/Games_report.csv --mode wide
```

Long diagnostic mode:

```text
whdtlv_report --tlv output/Games.tlv --defs assets_raw/defs --out output/Games_report_long.csv --mode long
```

Recommended options:

```text
--tlv <path>          input TLV file
--defs <path>         CSV definition folder
--out <path>          output CSV file
--mode wide|long      output layout, default wide
--include-ids         include raw numeric IDs beside resolved text
--include-desc        include CSV description column where available
--include-status      include resolution/debug status columns
--multi-only          only export groups with more than one variant
--problems-only       only export unresolved/suspicious rows
--help                print usage
```

Keep the command-line parser simple and portable. This should build cleanly on Windows host builds first.

---

## Output Mode 1 - Wide CSV

Wide mode should produce one row per archive/variant.

Example:

```csv
group_id,group_name,display_name,archive_size_kib,archive_crc32,chipset,language,memory,version
145,AlienBreed,AlienBreed_v1.3_AGA_En_Fr.lha,1234,A1B2C3D4,AGA,"En;Fr",FAST2M,1.3
```

Wide mode is the default because it is Excel-friendly. Users can filter, sort, and inspect variants quickly.

### Wide Mode Multi-Value Handling

If a field appears multiple times for one variant, join the resolved values into a single cell using a semicolon delimiter:

```text
En;Fr;De
```

If `--include-ids` is enabled, include matching ID columns:

```csv
language_ids,language
"4;10;7","En;Fr;De"
```

If `--include-desc` is enabled, include descriptions:

```csv
language_ids,language_tokens,language_descriptions
"4;10;7","En;Fr;De","English;French;German"
```

Use CSV escaping correctly. Any cell containing commas, quotes, CR/LF, or semicolons should be safely quoted. Quotes inside values must be doubled.

### Wide Mode Column Construction

The tool should be field-map driven rather than hard-coded.

Recommended leading columns:

```text
group_id
group_name
display_name
archive_size_kib
archive_crc32
```

Then append one or more columns for each TLV field found in the field map, excluding special fields already emitted separately:

```text
chipset
language
memory
version
publisher
software_houses
contributors
crack_groups
...
```

Avoid assuming the Games schema. The same reporting code should work for future pack types with different fields.

---

## Output Mode 2 - Long CSV

Long mode should produce one row per stored field value.

Example:

```csv
group_id,group_name,display_name,field_id,field_name,value_index,raw_value,resolved_token,resolved_description,status
145,AlienBreed,AlienBreed_v1.3_AGA_En_Fr.lha,6,chipset,0,3,AGA,Advanced Graphics Architecture,ok
145,AlienBreed,AlienBreed_v1.3_AGA_En_Fr.lha,7,language,0,4,En,English,ok
145,AlienBreed,AlienBreed_v1.3_AGA_En_Fr.lha,7,language,1,10,Fr,French,ok
145,AlienBreed,AlienBreed_v1.3_AGA_En_Fr.lha,8,version,0,1.3,1.3,,string_field
```

Long mode is the best format for debugging because it shows every TLV value individually.

### Long Mode Required Columns

Use these columns as a starting point:

```text
group_id
group_name
display_name
field_id
field_name
value_index
raw_value
resolved_token
resolved_description
status
```

Optional extra columns if easy:

```text
value_length
csv_file
is_multi_value
is_special_field
```

---

## Multi-Value Field Requirements

The exporter must not assume that one field appears only once per variant.

Fields such as the following may be multi-value:

```text
language
sps
contributors
crack_groups
software_houses
variant_tags
compilations
```

Do not hard-code this list as the only possible multi-value fields. Treat repeated field IDs as valid unless the TLV parser proves otherwise.

Required behaviour:

- Preserve every repeated value.
- Preserve value order as encountered in the TLV.
- In wide mode, join repeated values with semicolons.
- In long mode, emit one row per value and set `value_index` to 0, 1, 2, etc. within that field for that variant.
- Count repeated fields in `multi_value_fields_seen`.
- Do not silently drop duplicates. Duplicates may be diagnostically important.

---

## Value Resolution

The tool should resolve numeric TLV values back to CSV text where possible.

For CSV-backed token fields:

- Read the current CSV file from `defs_dir`.
- Match the stored numeric ID to the CSV row ID.
- Export the token column.
- Export the description column when `--include-desc` is enabled.

For free-form string fields:

- Export the stored string value as-is.
- Set status to `string_field` if status columns are enabled.

For special fields:

- `display_name`: used as the variant boundary/name.
- `group_id`: decode as uint16 big-endian.
- `archive_info`: decode archive size KiB and CRC32 as uint32 big-endian values.

Be careful with existing mixed-endian TLV rules:

- TLV record value lengths are little-endian.
- Most token IDs are stored as little-endian uint32.
- `group_id` is stored as big-endian uint16.
- `archive_info` sub-fields are stored as big-endian uint32.

Do not guess endianness from the host platform.

---

## CSV File Linkage

The exporter needs to know which TLV field maps to which CSV file.

Preferred approach:

1. Use the TLV field map to enumerate field names.
2. Use the existing pack type / field registry / CSV loading mechanisms where practical.
3. Match field names to CSV definition files in the same way the builder and filter already do.
4. If no CSV backing exists, treat the field as string or unresolved depending on the TLV value type and existing metadata.

Do not hard-code only `Chipset.csv`, `Language.csv`, and `Memory.csv`. The user-defined system should remain field-driven.

---

## Resolution Status Values

Use simple status strings. Suggested values:

```text
ok
string_field
special_field
missing_csv
not_in_csv
malformed_length
unknown_field_type
empty_value
```

Meaning:

| Status | Meaning |
|---|---|
| `ok` | Value resolved cleanly through CSV or known handling. |
| `string_field` | Field is a free-form string value. |
| `special_field` | Field is handled specially, such as group_id or archive_info. |
| `missing_csv` | Field appears to be CSV-backed but the CSV file could not be found/read. |
| `not_in_csv` | Numeric ID was present but no matching CSV row exists. |
| `malformed_length` | TLV value length is invalid for the expected type. |
| `unknown_field_type` | The exporter cannot determine whether the field is token or string. |
| `empty_value` | Field is present with no usable payload. |

In `--problems-only` mode, export only rows/variants with statuses other than `ok`, `string_field`, or `special_field`.

---

## Group Reporting

The export must include `group_id` for every variant where available.

Also include `group_name` if the TLV group map is present.

If no group map is present:

- Fall back to the existing display-name heuristic, if available.
- Make it clear in status/summary that the group name was derived, not read from the TLV group map.

The `--multi-only` option should restrict output to groups containing more than one variant. This is useful for inspecting cases where a game has multiple versions, chipsets, languages, editions, or other variants.

Required group-related summary counters:

```text
groups_total
multi_variant_groups_seen
variants_total
rows_written
```

---

## Archive Info

If the TLV contains `archive_info`, export:

```text
archive_size_kib
archive_crc32
```

The CRC should be written as 8 uppercase hexadecimal characters, for example:

```text
4AF2C824
```

If `archive_info` is absent, leave these cells empty rather than failing the export.

If malformed, set status to `malformed_length` in long mode or in a wide-mode status column if `--include-status` is enabled.

---

## Error Handling

Suggested return codes may reuse existing project style, but keep reporting errors distinct if helpful.

Minimum failure cases:

| Case | Behaviour |
|---|---|
| Missing required argument | Return invalid argument error. |
| TLV cannot be opened | Return I/O error. |
| TLV header is invalid | Return parse error. |
| Output CSV cannot be written | Return I/O error. |
| Allocation failure | Return allocation error. |
| CSV file missing | Do not fail by default; mark affected values as `missing_csv`. |
| CSV ID not found | Do not fail by default; mark affected values as `not_in_csv`. |

The exporter should be tolerant. Its job is to help diagnose bad or stale data, so unresolved CSV values should generally appear in the report rather than aborting the whole export.

Optional later strict mode may fail on missing CSV or unresolved IDs, but do not make that the default.

---

## Memory Footprint

Host-side implementation can prioritise clarity over extreme memory savings, but avoid unnecessary whole-file duplication.

Important design constraints:

- Do not compile reporting code into the Amiga WHDFetch runtime by default.
- Avoid adding reporting API calls to the lean filtering facade unless explicitly requested later.
- Prefer streaming CSV output once variants have been parsed.
- If the existing TLV reader already loads the full TLV into memory, reuse that path rather than creating a second copy.
- Avoid building huge temporary strings for wide-mode rows; use a small growable buffer or write escaped cells incrementally.

---

## Build Integration

The agent should inspect the existing build system before editing.

Expected work:

1. Add `src/whdtlv/reporting/` sources to the host build only.
2. Add `tools_src/whdtlv_report/main.c` as a new host executable.
3. Do not add the reporting subsystem to the default Amiga build unless the project already has an explicit host/tool separation flag that makes this safe.
4. Keep C dialect compatible with the rest of the project. If the surrounding WHDTLV code is C89-compatible, write this in C89 style.
5. Avoid C library calls that are not already used elsewhere in the project portability layer.

---

## Implementation Stages

### Stage 1 - Inventory Existing TLV Reader Code

Before coding, inspect existing modules that already:

- Load the TLV into memory.
- Parse the field map block.
- Parse the group map block.
- Scan variants from `data_offset`.
- Decode `display_name`.
- Decode `group_id`.
- Decode `archive_info`.
- Load CSV definitions.
- Resolve CSV IDs to tokens/descriptions, or at least resolve tokens to IDs.

Reuse these where practical. Do not blindly duplicate parsing logic if stable helpers already exist.

### Stage 2 - Define Reporting Types

Create `whdtlv_report_csv.h` and any supporting type header.

Add:

- Options struct.
- Summary struct.
- Mode enum.
- Defaults function.
- CSV export function.

Keep the API compact.

### Stage 3 - Parse TLV Into an Inspection View

Create an internal inspection model that supports repeated fields:

```text
variant
  display_name
  group_id
  group_name
  archive_size_kib
  archive_crc32
  fields[]
    field_id
    field_name
    values[]
      raw bytes / raw numeric / string
      value length
      resolved token
      resolved description
      status
```

The internal representation can be simpler than this if CSV output can be streamed correctly, but it must not lose repeated field values.

### Stage 4 - Implement CSV Resolution

For each field value:

- Determine whether it is a special field, token field, or string field.
- For token fields, resolve numeric ID to CSV token/description.
- For string fields, export raw string.
- Track status.

The implementation should handle missing CSV files and missing IDs gracefully.

### Stage 5 - Implement Wide Mode

- Build field-driven column headers.
- Emit one row per variant.
- Join multi-values with semicolons.
- Include group and archive columns.
- Honour `--include-ids`, `--include-desc`, `--include-status`, `--multi-only`, and `--problems-only` where practical.

### Stage 6 - Implement Long Mode

- Emit one row per stored field value.
- Include `value_index` per field per variant.
- Include raw and resolved values.
- Include status.
- Honour filtering options.

### Stage 7 - Add CLI Tool

Implement `tools_src/whdtlv_report/main.c`.

Required behaviour:

- Parse command-line arguments.
- Call `whdtlv_report_options_defaults()`.
- Call `whdtlv_report_csv_file()`.
- Print a concise summary.
- Return non-zero on fatal errors.

Example summary:

```text
TLV report export complete
TLV      : output/Games.tlv
Defs     : assets_raw/defs
Output   : output/Games_report.csv
Mode     : wide
Variants : 3973
Groups   : 2904
Rows     : 3973
Resolved : 18422
Problems : 0
```

### Stage 8 - Tests

Add host tests first.

Minimum tests:

1. Export a known TLV in wide mode.
2. Export the same TLV in long mode.
3. Confirm row count in wide mode equals variant count.
4. Confirm long mode row count is greater than or equal to variant count.
5. Confirm `group_id` appears in both modes.
6. Confirm `group_name` appears when group map exists.
7. Confirm repeated field values are preserved.
8. Confirm wide mode joins repeated values with semicolons.
9. Confirm long mode emits repeated values as separate rows with increasing `value_index`.
10. Confirm archive CRC is uppercase 8-character hex where archive_info exists.
11. Confirm missing CSV does not abort export and produces `missing_csv` statuses.
12. Confirm `--multi-only` suppresses single-variant groups.
13. Confirm `--problems-only` exports only problem rows/variants.
14. Confirm CSV escaping works for commas, quotes, CR/LF, and semicolons.
15. Confirm invalid TLV returns a parse error.
16. Confirm invalid output path returns an I/O error.

If possible, include a tiny synthetic TLV fixture specifically containing a repeated field such as multiple language values.

---

## Excel CSV Rules

Implement proper CSV escaping:

- Separate cells with commas.
- Quote cells containing commas, quotes, CR, LF, or leading/trailing spaces.
- Inside quoted cells, double each quote character.
- Use CRLF or LF consistently based on existing project conventions. Prefer CRLF if the file is primarily intended for Excel on Windows.
- Do not write a UTF-8 BOM unless there is already a project convention for it. If non-ASCII text becomes common, consider an optional BOM later.

---

## Non-Goals For First Version

Do not implement these in the first pass unless they are trivial:

- Applying profiles or scoring.
- Explaining why a specific profile selected or rejected a variant.
- Rebuilding TLV files.
- Editing CSV files.
- GUI display.
- Amiga-side CSV export.
- JSON output.
- HTML reports.

These can be future reporting features once the basic CSV exporter is stable.

---

## Future Extensions

The reporting subsystem name should leave room for future reports, such as:

```text
--summary
--groups
--field-map
--csv-fingerprints
--unknowns
--duplicates
--profile-explain
```

Possible future outputs:

- CSV group summary.
- CSV field usage summary.
- List of groups with the most variants.
- List of unresolved IDs.
- Comparison between two TLV files.
- Explanation report for why a profile selected one variant over another.

Do not design the first implementation in a way that prevents these later.

---

## Acceptance Criteria

The work is complete when:

1. A new reusable reporting subsystem exists under `src/whdtlv/reporting/`.
2. A new host command-line tool exists under `tools_src/whdtlv_report/`.
3. The tool can export a prebuilt TLV to wide CSV.
4. The tool can export a prebuilt TLV to long CSV.
5. Both modes include `group_id`.
6. Both modes include `group_name` when the group map is present.
7. Multi-value fields are preserved correctly.
8. CSV-backed numeric values are resolved to tokens where possible.
9. Descriptions can be included where available.
10. Missing CSV files or unknown IDs are reported in output rather than causing a default fatal error.
11. `archive_info` is exported when present.
12. The existing filtering facade remains unchanged unless there is a strong reason to touch it.
13. The reporting subsystem is not included in the default Amiga runtime build.
14. Host tests pass.
15. Existing filter tests still pass.

---


