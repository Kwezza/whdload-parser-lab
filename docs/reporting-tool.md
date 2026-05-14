# TLV Reporting Tool

`whdtlv_report` is a host-side command-line tool that reads a prebuilt `.tlv` file,
resolves stored numeric field values back to human-readable tokens using the asset CSV
definitions, and writes a CSV file suitable for inspection in Excel or any spreadsheet
tool.

It is **host-only** and has no Amiga build target.

---

## Building

```bat
make report
```

The binary is written to the project root as `whdtlv_report.exe` (Windows) or
`whdtlv_report` (Linux/macOS).

---

## Usage

```
whdtlv_report.exe --tlv <file.tlv> [options]
```

### Required

| Argument | Description |
|----------|-------------|
| `--tlv <path>` | Input `.tlv` file to decode |

### Options

| Argument | Description | Default |
|----------|-------------|---------|
| `--defs <dir>` | Asset CSV definitions directory | `assets_raw/defs` |
| `--out <path>` | Output CSV file path | `<tlv-basename>.csv` |
| `--mode wide\|long` | Export layout (see below) | `wide` |
| `--include-ids` | Add `_ids` columns with raw numeric token IDs | off |
| `--include-desc` | Add `_descriptions` columns with long CSV descriptions | off |
| `--include-status` | Add `_status` columns with resolution status codes | off |
| `--include-effective` | Add `_effective` companion columns for CSV-backed TOKEN fields | off |
| `--multi-only` | Only export groups that contain more than one variant | off |
| `--problems-only` | Only export rows where at least one field failed to resolve | off |
| `--help` | Print usage and exit | — |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Bad arguments |
| 2 | TLV file could not be opened |
| 3 | TLV structure is invalid |
| 4 | Output CSV could not be created |
| 5 | Out of memory |
| 9 | Unknown error |

---

## Output Modes

### Wide mode (default)

One row per variant.  Where a field can have multiple values (e.g. a game with two
languages), the values are joined in a single cell with `;` as the separator.

```
group_id, variant_name, chipset, language, memory, ...
AbcGame,  AbcGame_v1.0_AGA_De_Fr, AGA, De;Fr, 2MB, ...
```

This is the most convenient layout for filtering and pivot tables in Excel.

### Long mode (`--mode long`)

One row per stored field value.  A `field` column names the field and a
`raw_value` column holds the decoded cell.  A `value_index` column tracks
repeated fields.  Use this layout for debugging or when you need to audit every
individual value stored in the TLV.

```
group_id, variant_name, field,    value_index, raw_value, token, ...
AbcGame,  AbcGame_v1.0, chipset,  0,           1,         AGA,   ...
AbcGame,  AbcGame_v1.0, language, 0,           64,        De,    ...
AbcGame,  AbcGame_v1.0, language, 1,           32,        Fr,    ...
```

---

## Column Reference

The columns present in a wide-mode CSV depend on which fields are stored in the
TLV.  The following fields are produced by the standard pipeline:

| Column | Source | Notes |
|--------|--------|-------|
| `group_id` | TLV group key | Shared across all variants of the same game |
| `variant_name` | `display_name` field | Full variant filename from the DAT |
| `chipset` | `Chipset.csv` | e.g. `OCS`, `ECS`, `AGA` |
| `language` | `Language.csv` | 2-byte bitmask; multi-language joined with `;` |
| `memory` | `Memory.csv` | e.g. `512KB`, `1MB`, `2MB` |
| `media` | `Media.csv` | e.g. `Disk`, `CD32`, `CDTV` |
| `video` | `Video.csv` | e.g. `PAL`, `NTSC` |
| `disks` | `Disks.csv` | Number of floppy disks |
| `special` | `Special.csv` | Special release tags |
| `variant_tags` | `variant_tags.csv` | Additional variant classifiers |
| `sps` | *(no CSV)* | SPS catalog number stored as decimal integer |
| `archive_info` | *(internal)* | Rendered as `CRC32=XXXXXXXX size=NNNN` |

With `--include-ids`, each resolved field gains a companion `<field>_ids` column
containing the raw numeric value (or bitmask value for language).

With `--include-desc`, each resolved field gains a `<field>_descriptions` column
containing the long-form description from the CSV (falls back to the short token
when no separate description is present).

With `--include-status`, each field gains a `<field>_status` column with one of
the following codes:

| Status | Meaning |
|--------|---------|
| `ok` | Resolved to a token successfully |
| `string_field` | Free-form string, no CSV lookup needed |
| `special_field` | Special structural field (archive_info, display_name) |
| `missing_csv` | No backing CSV exists for this field |
| `not_in_csv` | Numeric ID was not found in the CSV |
| `malformed_length` | Payload length did not match the expected encoding |
| `empty_value` | Field was present but had zero-length payload |

---

## Effective Value Columns (`--include-effective`)

Some TLV variants omit optional fields such as `language` or `video` because the
pipeline did not detect an explicit value in the source DAT.  When that happens the
raw column is blank and no resolution occurs, which makes the CSV harder to use for
filtering (a blank `language` cell cannot be treated the same as `En`).

`--include-effective` adds a `<field>_effective` companion column for every
CSV-backed TOKEN field.  The companion column always contains a value — either the
explicit value already stored in the TLV, or the CSV default when no explicit value
was stored.

### How the default is determined

The backing CSV can mark exactly one row as the default by appending `,default` as
a fourth column.  For example `Language.csv`:

```
1,Cz,Czech
2,Dk,Danish
3,NL,Dutch
4,En,English,default
5,Fi,Finnish
...
```

Row 4 is the default.  Any variant that stores no explicit language value will have
`language_effective=en` (the short token, lowercased).

The `,default` marker is matched case-insensitively and trailing whitespace or CRLF
is stripped, so `default`, `Default`, `default `, and `default\r\n` are all
recognised.

### Effective source

When `--include-status` is also active an `<field>_effective_status` column is
added that records how the effective value was chosen:

| Effective status | Meaning |
|------------------|---------|
| `explicit` | The TLV contains an explicit value for this field; the effective column mirrors it |
| `default` | No explicit value was stored; the effective column carries the CSV default |
| `missing` | No explicit value and no CSV default row defined; the effective column is blank |
| `invalid_default` | More than one row in the CSV carries `,default`; the result is ambiguous (data-quality issue) |

### Companion column order

For each TOKEN field the column order in wide mode is:

```
<field>  [<field>_ids]  [<field>_descriptions]  [<field>_status]
<field>_effective  [<field>_effective_descriptions]  [<field>_effective_ids]  [<field>_effective_status]
```

In long mode a synthetic row is appended for each TOKEN field with a CSV default
that was absent from the variant.  The explicit columns (`raw_value`,
`resolved_token`, `resolved_description`, `status`) are blank; the effective
columns carry the default value with `effective_status=default`.

### Which fields are affected

Only CSV-backed TOKEN fields gain companion columns.  String fields (`version`,
`sps`) and structural fields (`archive_info`, `display_name`) do not.  The
following fields from the standard pipeline have CSV defaults and therefore always
produce a non-blank effective value:

| Field | CSV file | Default |
|-------|----------|---------|
| `chipset` | `Chipset.csv` | `OCS` (id 1) |
| `language` | `Language.csv` | `En` (id 4) |
| `video` | `Video.csv` | `PAL` (id 1) |

---

## Filtering Options

### `--multi-only`

Skip any group that contains only a single variant.  Use this to produce a CSV
of every game (or demo) for which more than one version exists — useful for
comparing which attributes differ between variants.

### `--problems-only`

Skip rows where every field resolved cleanly.  Use this to surface entries that
have unrecognised IDs or malformed payloads.

The two flags can be combined.

---

## The Asset Definitions Directory

The `--defs` directory must contain the CSV files listed above.  The default path
`assets_raw/defs` is relative to the working directory, so run the tool from the
project root or supply an absolute path.

Each CSV is formatted as:

```
<id>,<short_token>[,<long_description>[,<flags>]]
```

For example `Language.csv`:

```
1,Cz,Czech
2,Dk,Danish
3,NL,Dutch
4,En,English,default
5,Fi,Finnish
6,Fr,French
7,De,German
...
```

The tool resolves numeric IDs to short tokens for the main column and to long
descriptions when `--include-desc` is active.

---

## Examples

### Basic export — wide mode, all variants

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv
```

Writes `output\GamB(2026-04-26).csv` next to the TLV using the default
`assets_raw/defs` definitions.

---

### Export only games with multiple versions

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --out output\multi_versions.csv ^
    --multi-only
```

Produces one row per variant, but only for groups where more than one variant
exists.  Good starting point for a "version comparison" spreadsheet.

---

### Full wide export with descriptions

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --out output\games_full.csv ^
    --include-desc ^
    --include-ids
```

Adds `<field>_descriptions` and `<field>_ids` companion columns alongside every
resolved field.

---

### Long mode — one row per field value

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --out output\games_long.csv ^
    --mode long ^
    --include-status
```

Writes one row per stored field value.  The `_status` column shows the resolution
outcome for every value, which is useful for diagnosing `not_in_csv` or
`malformed_length` entries.

---

### Export demos, showing only problem rows

```bat
whdtlv_report.exe --tlv output\DemB(2026-04-20).tlv ^
    --out output\demo_problems.csv ^
    --problems-only ^
    --include-status
```

Only rows where at least one field failed to resolve are written.  Combine with
`--include-status` to see which specific field caused the problem.

---

### Export with effective defaults — wide mode

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --out output\games_effective.csv ^
    --include-effective ^
    --include-desc
```

Adds `<field>_effective` and `<field>_effective_descriptions` companion columns
alongside every CSV-backed TOKEN field.  Variants that store no explicit `language`
value get `language_effective=en` / `language_effective_descriptions=English`
instead of a blank cell.

---

### Audit effective defaults with source tracking — long mode

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --out output\games_effective_long.csv ^
    --mode long ^
    --include-effective ^
    --include-status
```

Writes one row per stored field value; missing TOKEN fields with a CSV default get
a synthetic row (`effective_status=default`).  Fields explicitly stored in the TLV
get `effective_status=explicit`.  Use this to audit which values came from the TLV
versus which were inferred from the CSV defaults.

---

### Export with a custom definitions directory

```bat
whdtlv_report.exe --tlv output\GamB(2026-04-26).tlv ^
    --defs C:\Amiga\Assets\defs ^
    --out C:\Amiga\Reports\games.csv
```

---

## Summary Output

On success the tool prints a summary table to stdout:

```
--- Export summary ---
  Output file          : output\multi_versions.csv
  Groups scanned       : 107
  Variants scanned     : 40
  Rows written         : 40
  Fields written       : 68
  Values resolved      : 68
  Values unresolved    : 0
  Problem rows         : 0
  Multi-value fields   : 0
  Multi-variant groups : 19
----------------------
```

When `--include-effective` is active three additional counters appear:

```
  Effective explicit   : 12
  Effective default    : 84
```

`Effective explicit` counts field-variant pairs where the TLV contained an
explicit value (the effective column mirrors the raw value).  `Effective default`
counts field-variant pairs where the effective column was filled from the CSV
default because no explicit value was stored.  If any CSV has more than one default
row an `Effective inv_def` counter is also printed with a `WARNING` tag.

`Multi-variant groups` is the count of groups that contained more than one variant
regardless of whether `--multi-only` was used.

---

## Notes

- The tool is **host-only**.  Do not compile it for the Amiga target.
- `language` is stored in the TLV as a 16-bit bitmask (one bit per language ID).
  Multi-language variants such as `_DeFr` are decoded to `De;Fr` / `German;French`.
- `sps` has no backing CSV; it is always rendered as a raw decimal catalog number.
- `archive_info` is an 8-byte internal field rendered as `CRC32=XXXXXXXX size=NNNN`.
- If the output CSV is open in Excel when you run the tool, it will fail with exit
  code 4.  Close the file first.
- With `--include-effective`, a variant that stores an explicit multi-language value
  (e.g. `De;Fr`) is always recorded as `explicit`; the language default is never
  mixed into an existing bitmask value.
- The `--include-effective` flag is silently ignored for string fields (`version`,
  `sps`) and structural fields (`archive_info`).  Those fields have no CSV default
  concept and no `_effective` column is emitted for them.
