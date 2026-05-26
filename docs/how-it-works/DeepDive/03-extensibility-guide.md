# Deep Dive 3 — Extensibility Guide

> **Audience:** Contributors and power users who want to extend or customise the system
> without modifying or recompiling source code.  
> Assumes you have already read [01-architecture-and-creation-pipeline.md](01-architecture-and-creation-pipeline.md)
> and [02-filtering-system.md](02-filtering-system.md).

---

## Core Philosophy

`dat_to_tlv` is designed as a **framework controlled by configuration files**.
Tokens, fields, pack types, and filter preferences are all declared in plain-text
files that live alongside the binary. Virtually all domain-specific knowledge —
which chipsets exist, how memory tokens are spelled in DAT filenames, what machine
a profile describes — lives in [assets_raw/defs/](../../../assets_raw/defs/),
[assets_raw/prefs/pack_types.ini](../../../assets_raw/prefs/pack_types.ini), and
[assets_raw/profiles/](../../../assets_raw/profiles/).

There are four extension points, in order of increasing scope:

| Extension | Files touched | Recompile? | Rebuild TLV? |
|-----------|--------------|------------|-------------|
| Add a token to an existing field | One CSV in `assets_raw/defs/` | No | **Yes** |
| Add a new field (CSV-backed token field only) | `pack_types.ini` + new CSV | No | **Yes** |
| Add a new pack type | `pack_types.ini` | No | **Yes** |
| Create or modify a filter profile | New `.profile` in `assets_raw/profiles/` | No | No |

None of these require touching C source code. The one category that **does**
require recompilation is described in [Limitations](#limitations) at the end of
this document.

---

## Extension 1 — Add a Token to an Existing Field

### How it works

Each field (chipset, memory, language, …) has its own CSV file in
[assets_raw/defs/](../../../assets_raw/defs/). When the pipeline runs,
`csv_cache_load_all()` reads every CSV that is referenced by the active pack
type's `FieldList`. Tokens found in DAT filenames are compared against these
rows at runtime; the matching numeric ID is stored in the TLV.

The CSV format is:

```
ID, filename_token, description[, default]
```

- **`ID`** — numeric identifier stored in the TLV. Each distinct meaning must
  have its own unique ID; however, multiple rows may deliberately share the same
  ID when they are aliases for the same value (see alias rows below).
- **`filename_token`** — the token spelling matched when the filename is split on
  underscores. The tokeniser splits the filename into discrete parts at every `_`
  boundary; single-part tokens are compared whole against this column, and
  multi-word tokens (configured via `prescan.multi_token`) are matched as a
  contiguous span of parts rejoined with `_`. In both cases matching is exact
  equality after case-normalisation — it is **not** arbitrary substring search
  inside the full filename string.
- **`description`** — human-readable label used in logs and reports.
- **`default`** (optional, fourth column) — the literal word `default`. At most
  one row per file should carry this marker; it identifies the token to assume
  when no other token matches.

Multiple rows may share the same numeric `ID`. The **first** row for an ID is the
**canonical token** — the name that should appear in profile `include=`/`exclude=`
lists and the name returned for display and reporting. Later rows with the same ID
are **alias rows**: they are matched against DAT filenames at build time and
resolve to the same stored ID, but they are never returned by a reverse lookup.
This distinction is enforced in the code: `csv_cache_insert()` marks the first row
for each ID as `is_canonical` and `csv_cache_reverse_lookup()` only returns entries
with that flag set.

For full alias-row rules see [Chipset.csv](../../../assets_raw/defs/Chipset.csv)
and the preamble comments inside [Memory.csv](../../../assets_raw/defs/Memory.csv).

### Worked example — add a token to `Chipset.csv`

Suppose you want to recognise `RTG` as a distinct chipset category. Open
[assets_raw/defs/Chipset.csv](../../../assets_raw/defs/Chipset.csv):

```
1,OCS,Original Chip Set,default
2,ECS,Enhanced Chip Set
3,AGA,Advanced Graphics Architecture
4,CD32,CD32 version
5,CDTV,CDTV version
```

Append a new row with the next available ID:

```
6,RTG,ReTargetable Graphics
```

That is the entire change. The next time `dat_to_tlv` runs it will pick up the
new token, assign ID `6` to any filename that contains `RTG`, and embed it in the
TLV. To filter on it, add `RTG` to `include=` or `exclude=` in a `.profile` file.

> **Note on alias rows:** If DAT filenames spell the token as both `RTG` and
> `P96`, add a second row with the same ID:
>
> ```
> 6,RTG,ReTargetable Graphics
> 6,P96,Alias for RTG
> ```
>
> The canonical token for profiles is `RTG`; `P96` will match at build time and
> resolve to the same stored ID.

### You must rebuild the TLV after any CSV change

When `dat_to_tlv` writes a TLV it embeds a **CRC-32 fingerprint** for every CSV
that contributed to that build (stored in header block `0x04`). At filter time
`tlv_crc_validate.c` iterates over that embedded fingerprint list and recomputes
the CRC of each named CSV. If they differ — because you edited an existing row —
the check fails and filtering is aborted.

**Adding a brand-new field** (Extension 2) requires a rebuild for a different
reason: the old TLV has no fingerprint entry for the new CSV and no field-map
entry for the new field. The CRC validator will not raise a mismatch (it only
checks CSVs that the TLV already knows about); instead, the new field is simply
absent from the TLV's embedded field map and produces no results. The practical
outcome is the same — rebuild — but the diagnostic is "field not in TLV" rather
than "CRC mismatch".

Editing a profile (Extension 4) does **not** trigger a CRC failure because
profiles are not fingerprinted in the TLV.

---

## Extension 2 — Add a New Field

> **Scope of this extension:** The no-recompile path described here applies to
> **plain CSV-backed filename-token fields** — fields whose values are extracted
> by matching underscore-split filename parts against a CSV lookup table. Fields
> that need a different extraction model require C source changes and a rebuild:
>
> | Field type | Example | Why it needs code |
> |-----------|---------|-------------------|
> | Structured binary payload | `archive_info` | Injected as an 8-byte big-endian size+CRC block by `whdtlv_integration.c`, no CSV involved |
> | Post-processing derived field | `group_id` | Injected after all records are built by `tlv_session_inject_group_ids()` in `tlv_builder.c` |
> | Custom parsing rule | `version`, `language`, `sps` | Handled by dedicated parser functions excluded from the generic CSV-match path in `pack_field_uses_generic_csv_match()` |
> | Prescan with custom logic | `contributors` | Excluded from generic CSV match; extracted by the prescan pass with its own multi-token behaviour |
>
> If your new field maps one filename token to one numeric ID and does not need
> special payload encoding, proceed below. Otherwise a source change is required.

Adding a new CSV-backed filename-token field involves two steps: declaring it in
`pack_types.ini` and supplying a CSV file for its tokens.

### Step 1 — create the CSV

Create a new file in [assets_raw/defs/](../../../assets_raw/defs/) named after the
field. The filename (without `.csv`) must match the field name you will add to
`FieldList`. By default `set_csv_base_for_field()` in `field_registry.c` uses
the field name as-is — so a field named `controller` must have a file named
`controller.csv` (all lowercase). The only exception in the current code is
`language`, which is mapped to `Language.csv` via a hardcoded special case.
Do not capitalise new CSV filenames unless you also add a matching override in
`set_csv_base_for_field()` (which requires a recompile).

Follow the same `ID, token, description` format described above. Include a
preamble comment block explaining what the field represents and any alias
conventions.

### Step 2 — add the field to `pack_types.ini`

Open [assets_raw/prefs/pack_types.ini](../../../assets_raw/prefs/pack_types.ini).
Every pack type has a pipe-delimited entry in `[PackTypes]` whose last column is a
comma-separated `FieldList`. Append the new field name to the `FieldList` of each
pack type that should extract it.

For the full column reference see [docs/pack-types-ini-format.md](../../pack-types-ini-format.md).

### Step 3 (optional) — configure `[FieldAttributes]`

If the field needs special extraction behaviour, add entries to the
`[FieldAttributes]` section of `pack_types.ini`:

| Attribute key | Effect |
|--------------|--------|
| `<field>.allow_multiple = true` | A single filename may match more than one token for this field. |
| `<field>.prescan.enabled = true` | Extract this field in a dedicated pre-pass before main tokenisation. Useful for multi-word tokens that would otherwise be split. |
| `<field>.prescan.order = <n>` | Lower numbers run earlier in the prescan pass. |
| `<field>.prescan.source = <field>` | Names the CSV field whose token list is used during the prescan. Normally matches the field name itself, but can point to a different field's CSV when two fields share a token table. |
| `<field>.prescan.remove_from_filename = true` | Matched tokens are stripped from the working filename before the main token loop runs. |
| `<field>.prescan.multi_token = true` | Multiple tokens may be extracted during the prescan pass for this field. |

### Worked example — add a `controller` field

Suppose you want to capture whether a game supports a specific controller
(e.g. `CD32Pad`, `Mouse`, `Lightgun`).

**1. Create** `assets_raw/defs/controller.csv`:

```
# controller.csv
# Controller-type tokens found in WHDLoad filenames.
1,CD32Pad,CD32 joypad
2,Mouse,Mouse required
3,Lightgun,Lightgun required
```

**2. Add** `controller` to the `FieldList` of pack type `1` (Games) in
`pack_types.ini`:

```ini
1 = Games|Games|...|Game|sps,publisher,...,archive_info,controller
```

**3. No `[FieldAttributes]` needed** unless filenames use multi-word tags or the
field can have more than one value per entry; in that case add
`controller.allow_multiple = true`.

The next build will register a `controller` field at runtime, scan all game
filenames for `CD32Pad`, `Mouse`, and `Lightgun`, and embed matches in the TLV.

As with any CSV change, any previously built TLV is now stale — its embedded CRC
fingerprint does not cover the new CSV — so you must rebuild the TLV before
filtering.

---

## Extension 3 — Add a New Pack Type

A pack type maps a class of DAT file (identified by its stem before the first
`(`) to a named set of fields. Adding a new one requires only an entry in
`[PackTypes]`.

### Entry format

```ini
ID = DisplayName|Abbreviation|SearchQuerySnippet|DatName|FieldList
```

Full column reference: [docs/pack-types-ini-format.md](../../pack-types-ini-format.md).

Key constraints:

- `ID` must be a positive integer not already used.
- `DatName` is the stem the pipeline matches against incoming DAT filenames
  (case-insensitive). Maximum **4 characters** due to Amiga filename limits.
- CSV-backed token fields should have a corresponding CSV file in
  `assets_raw/defs/`. If the CSV file is absent the field is registered in the
  field registry but produces no token matches; on an Amiga build a `WARNING`
  is written to the log. The host build is silent. This is non-fatal — the rest
  of the pack type continues to build normally.
- Some fields in the existing `FieldList` are **not** CSV-backed and need no CSV
  file. `set_csv_base_for_field()` in `field_registry.c` maps these to an empty
  basename so no load is attempted:

  | Field | Type |
  |-------|------|
  | `sps` | Numeric IDs extracted directly from the filename |
  | `display_name` | Internal string injected by the builder |
  | `archive_info` | 8-byte binary payload (size + CRC-32) injected by `whdtlv_integration.c` |
  | `group_id` | Structural key injected by `tlv_session_inject_group_ids()` |

  New fields you add that do not follow the plain CSV-token model will require C
  source changes to register a similar exemption and provide the custom extraction
  logic.

### Worked example — add a `Utils` pack type

```ini
6 = Utilities|Utils|Commodore%20Amiga%20-%20WHDLoad%20-%20Utilities%20(|Util|version,chipset,archive_info
```

Drop a DAT file named `Util(2025-01-01).dat` into the input folder and run
`dat_to_tlv`. The pipeline detects the stem `Util`, loads the `version`,
`chipset`, and `archive_info` fields, and writes a `Util(2025-01-01).tlv`
output file. No source changes needed.

---

## Extension 4 — Create or Modify a Filter Profile

A `.profile` file describes a machine configuration — which chipset, memory, and
language variants you want — and how strongly each preference should influence the
score when multiple candidates exist.

Profile files live in [assets_raw/profiles/](../../../assets_raw/profiles/).

### File structure at a glance

```ini
[Profile]
id=my_machine
name=My Machine
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.memory]
include=FAST4M,FAST2M,FAST1M
exclude=SLOW256K

[Scoring]
weight.chipset=150
weight.memory=100
```

- **`[Profile]`** — identity metadata. `id` must be unique across installed profiles.
- **`[Filter.<fieldname>]`** — one section per field you want to constrain. The
  part after the dot must exactly match a field name in `pack_types.ini`.
- **`[Scoring]`** — per-field weights. Higher numbers make that field's preference
  count more heavily in the tie-break score.

For the complete format reference, including the slash-bucket (multi-lane)
mechanism, see [docs/profile_system.md](../../profile_system.md).

> **Practical weight advice:** Scoring weights are most meaningful on `chipset`,
> `language`, and `memory` — the fields that most directly describe a machine
> configuration. Filtering on `variant_tags` or `contributors` is technically
> supported but unusual; weights on those fields are unlikely to produce the
> expected selection behaviour.

### `include=` and `exclude=` semantics

- Values are comma-separated canonical tokens (first row for each ID in the CSV).
- An empty `include` means "accept any value for this field".
- `exclude` is evaluated before `include`; a matching token immediately rejects the
  variant regardless of other scores.
- Token lookup is case-insensitive and normalised before matching. If a token is not
  found in the CSV, it is accepted into the list using an FNV-1a 8-bit hash of its
  name (see [What happens when something is unknown](#what-happens-when-something-is-unknown)).

### Selection buckets (slash syntax)

The slash `/` character divides an `include=` list into independent selection
**buckets**. Each bucket produces one winner per game group, so slashes allow the
filter to emit more than one variant per game:

```ini
[Filter.chipset]
include=AGA/ECS,OCS
```

This creates two lanes: lane 0 selects the best AGA variant; lane 1 selects the
best ECS or OCS variant. Both winners are written to the output for each group.
See [multi_bucket_reference.profile](../../../assets_raw/profiles/multi_bucket_reference.profile)
for a fully commented example.

### Profile hard limits

These compile-time constants cap what a profile can express without touching
source code:

| Constant | Value | What it limits |
|----------|-------|----------------|
| `FP_MAX_BUCKET_FIELDS` | 4 | Maximum number of fields in one profile that may use `/` buckets |
| `FP_MAX_BUCKETS_FIELD` | 8 | Maximum slash-separated buckets per `include=` list |
| `FP_MAX_SELECTION_LANES` | 32 | Maximum generated Cartesian-product lanes across all bucketed fields |
| `PB_MAX_TOKENS` | 32 | Maximum tokens in a single `include=` or `exclude=` list |

Exceeding a bucket or lane limit causes the profile to be **rejected** at load
time with an error (the profile is not used at all). Exceeding the token-list
limit (`PB_MAX_TOKENS`) causes excess tokens to be **silently discarded** — no
error is raised and `had_warnings` is not set, so keep include and exclude lists
short.

### Worked example — create a `cd32_pal.profile`

Create `assets_raw/profiles/cd32_pal.profile`:

```ini
[Profile]
id=cd32_pal
name=CD32 PAL
version=1
profile_format=1
debug=0

[Filter.chipset]
include=CD32,AGA,ECS
exclude=

[Filter.language]
include=EN
exclude=

[Scoring]
weight.chipset=200
weight.language=120
```

Run the filter against any Games TLV. How you reference the profile depends on
the calling context:

- **Public C API (`whdtlv_filter_to_list`):** pass the full file path to the
  `.profile` file as the `profile_path` argument, e.g.
  `"assets_raw/profiles/cd32_pal.profile"`. `NULL` or `""` means no profile.
- **Command-line harness / profile picker:** select the profile by its `id=`
  value (`cd32_pal`) or by filename, depending on how the front-end resolves
  profiles.

The `id=` field inside the file is metadata for profile pickers; it is not read
or used by the C API itself, which loads whatever path it is given.

---

## What Happens When Something Is Unknown

The runtime is designed to degrade gracefully rather than crash when it encounters
configuration it does not recognise.

### Unrecognised filter section

If a `[Filter.<fieldname>]` section names a field that does not exist in the
loaded TLV's embedded field map, `profile_binder.c` skips the section and sets
`had_warnings = 1` on the `WhdBoundProfile`. No log line is emitted at this
point — `debug=1` in `[Profile]` controls logging during the scoring pass, not
during profile loading. The profile continues to load and the filter runs
normally; the `had_warnings` flag is the only signal to the caller that something
was skipped. This means you can write profiles that reference fields only present
in some pack types without aborting filters on other pack types, but a typo in a
field name will be silently skipped with only the flag to indicate the problem.

### Token not in CSV

If a token value in `include=` or `exclude=` does not resolve to a row in the
corresponding CSV, `profile_binder.c` accepts it using an **FNV-1a 8-bit hash**
of the token name. The TLV builder uses the same hash for any filename token it
cannot resolve, so both sides agree on the numeric ID and matching still works.

This fallback is **completely silent** — no warning is emitted and `had_warnings`
is **not** set. A typo such as `AGG` instead of `AGA` will hash to a different
value than `AGA`'s CSV ID and silently produce no matches, with no diagnostic to
alert you. Double-check token spellings against the canonical first-row token in
the relevant CSV file.

`had_warnings` is set only in two situations: when a `[Filter.<field>]` or
`[Scoring]` section names a field that does not exist in the TLV's embedded
field map, and when the bound-field capacity limit (`PB_MAX_FIELDS`) is
exceeded.

### Unrecognised `[FieldAttributes]` key

Pack type loading ignores attribute keys it does not understand. Adding a new key
to `[FieldAttributes]` for a field that is otherwise valid will not cause a parse
error; the key is simply ignored until a future version of the loader is compiled
that handles it.

---

## Limitations

The following changes genuinely require recompiling the tool:

| Change | Why recompilation is needed |
|--------|-----------------------------|
| New prescan attribute type | The set of recognised prescan attributes (`enabled`, `order`, `source`, `multi_token`, `remove_from_filename`) is parsed by `pack_types_loader.c`. Adding a new attribute requires C code to handle it. |
| New field attribute semantics | Attributes beyond the current set in `[FieldAttributes]` are silently ignored; a new semantic requires a new code path. |
| New TLV block type | The header block type bytes (`0x01`, `0x02`, `0x04`) are defined in the C source. Adding a new block type requires changing [tlv_builder.c](../../../src/whdtlv/core/tlv_builder.c) and [tlv_reader.c](../../../src/whdtlv/filtering/tlv_reader.c). |
| New scoring algorithm | The per-field weight scoring model is implemented in `tlv_filter.c` and `tlv_select.c`. Changing the scoring model requires C changes. |
| Staged modules | `variant_iterator.c`, `variant_index.c`, `active_set.c`, `profile_loader.c`, `filter_pipeline.c`, and related staged files are not compiled and cannot be activated by configuration alone. |

> **Status note:** The staged modules above are present in the repository but are
> not listed in the Makefile `SRC` variable and therefore not part of the current
> build. See [01-architecture-and-creation-pipeline.md](01-architecture-and-creation-pipeline.md)
> for the full staged-module list.

---

## Quick Reference

| Want to… | File(s) to edit |
|----------|----------------|
| Recognise a new filename spelling for an existing token | CSV in [assets_raw/defs/](../../../assets_raw/defs/) — add an alias row with the same ID |
| Add a brand-new token category to an existing field | Same CSV — add a new row with a new ID |
| Add a new metadata field to a pack type | New CSV in `assets_raw/defs/` + field name in `FieldList` in [pack_types.ini](../../../assets_raw/prefs/pack_types.ini) |
| Add a new pack type for a new DAT family | New entry in `[PackTypes]` in [pack_types.ini](../../../assets_raw/prefs/pack_types.ini) |
| Create a filter profile for a new machine config | New `.profile` file in [assets_raw/profiles/](../../../assets_raw/profiles/) |
| Adjust scoring weights for an existing profile | Edit `[Scoring]` section in the relevant `.profile` file |

---

*Previous: [02-filtering-system.md](02-filtering-system.md)*
