# WHDLoad Manager - Profile System Reference

## Overview

Profile files (`.profile`) describe a **machine configuration** that the filter and scoring
engine uses to select the best variant of each game from the TLV data.  A profile is a
plain-text INI-style file with three sections:

- `[Profile]` - identity and debug settings
- `[Filter.<fieldname>]` - include and exclude token lists per field
- `[Scoring]` - per-field weighting values

Profile files live in:

    assets_raw/profiles/

The loader is implemented in `src_raw/profile_loader.c`, with the scoring engine in
`src_raw/filter_profile.c`.

---

## File Format

```ini
# Comment lines start with # or ;
[Profile]
id=my_profile
name=My Machine Profile
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.language]
include=EN
exclude=

[Filter.memory]
include=FAST8M,FAST4M,FAST2M,FAST1M
exclude=SLOW256K

[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
```

All values are trimmed of leading and trailing whitespace.  Lines that contain no `=` sign
are ignored.  The format is case-sensitive for field names but the token lookup against CSV
data is case-insensitive (tokens are normalised to lowercase before hashing).

---

## Section Reference

### [Profile]

| Key | Type | Description |
|-----|------|-------------|
| `id` | string (63 chars max) | Machine-readable identifier used when referencing this profile programmatically. Must be unique across all installed profiles. |
| `name` | string (127 chars max) | Human-readable display name shown in the GUI profile picker. |
| `version` | integer | File version for the specific profile content (user-managed). |
| `profile_format` | integer | Format version of the profile specification itself (currently `1`). |
| `debug` | 0 or 1 | Enable verbose diagnostic logging for this profile. Accepts `1`, `y`, `Y`, `t`, `T` as true. |

---

### [Filter.\<fieldname\>]

One section per field you want to filter on.  The section name after the dot must exactly
match a field name registered in `pack_types.ini` (see Field Registry below).

Each filter section supports two keys:

| Key | Description |
|-----|-------------|
| `include` | Comma-separated list of accepted token values in **priority order** (left = highest priority). An empty value means "accept all". |
| `exclude` | Comma-separated list of token values that **immediately reject** a variant. Takes precedence over include. |

Both keys are optional.  Omitting or leaving a key empty has the following effect:

- Empty `include` -> any value for that field is acceptable (not filtered out).
- Empty `exclude` -> nothing is explicitly rejected for that field.

The token values you supply are looked up against the corresponding CSV definition file
(see CSV Linkage below).  If a token does not resolve to a CSV entry, it is accepted into
the list using a hash of its name so it can still match against TLV records that were also
hashed the same way.

**Hard limits (from `filter_profile.h`):**

| Constant | Value | Meaning |
|----------|-------|---------|
| `FP_MAX_FIELDS` | 16 | Maximum number of different field filters per profile |
| `FP_MAX_INCLUDE` | 32 | Maximum tokens in a single include list |
| `FP_MAX_EXCLUDE` | 32 | Maximum tokens in a single exclude list |

Exceeding these limits causes the excess tokens to be silently discarded and
`ProfileMeta.had_warnings` is set to `true`.

---

### [Scoring]

Weights control how much each field contributes to the final variant score.

```ini
[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
```

| Attribute | Range | Notes |
|-----------|-------|-------|
| Minimum | 0 | Disables the field (not scored, but exclusions still apply) |
| Maximum | 255 | Values above 255 are clamped to 255 |
| Default (no entry) | 0 | A filter section without a corresponding weight line is active for exclusion but does not contribute points |

Any field name that appears in `weight.<fieldname>` must also be registered in the field
registry; unrecognised names are ignored and set `had_warnings`.

---

## Built-in Profiles

Three profiles are shipped in `assets_raw/profiles/`:

| File | id | Description |
|------|----|-------------|
| `pal_aga_4mb.profile` | `pal_aga_4mb` | Standard PAL AGA 4MB system (chipset 150, language 120, memory 100) |
| `chipset_aga_only.profile` | `chipset_aga_only` | Chipset filter only; AGA/ECS/OCS accepted, all weights on chipset |
| `chipset_legacy_only.profile` | `chipset_legacy_only` | ECS/OCS only; AGA explicitly excluded |

---

## Available Filter Fields

The filter engine recognises any field that is defined in `assets_raw/prefs/pack_types.ini`.
The built-in fields for the Games pack type are:

| [Filter.\<name\>] | CSV file | Notes |
|-------------------|---------|-------|
| `chipset` | `Chipset.csv` | Graphics chipset generation |
| `language` | `Language.csv` | Language of release |
| `memory` | `Memory.csv` | RAM requirement |
| `disks` | `Disks.csv` | Number of floppy disks |
| `media` | `Media.csv` | Distribution media type |
| `video` | `Video.csv` | Video standard (PAL / NTSC) |
| `variant_tags` | `variant_tags.csv` | Miscellaneous variant tags (NoIntro, Uncensored, etc.) |
| `version` | *(string field)* | Software version string |
| `sps` | *(multi-value)* | SPS/CAPS disc ID |
| `publisher` | *(string field)* | Publisher name |
| `software_houses` | *(string field)* | Software house name |
| `contributors` | *(multi-value)* | Preservation contributors |
| `crack_groups` | *(multi-value)* | Cracker group names |
| `cover_disks` | *(string field)* | Cover disk source |
| `compilations` | *(string field)* | Compilation membership |

Additional fields exist for Demos, Magazines, and Beta packs (`scene_group`, `issue`,
`magazines`).  See `pack_types.ini` for the full list.

> **Practical advice:** Filter weights are most meaningful on `chipset`, `language`, and
> `memory` because those fields have a direct bearing on whether a game runs on a given
> machine.  Filtering on `variant_tags` or `contributors` is supported but unusual.

---

## CSV Linkage - How Tokens Are Resolved

When the profile loader encounters a token such as `AGA` in `include=AGA,ECS,OCS`, it
performs the following resolution steps:

1. **Find the associated CSV** - The field name (`chipset`) is looked up in the
   `FieldRegistry`, which maps it to a CSV filename (`Chipset.csv`).
2. **CSV lookup** - `csv_cache_lookup()` searches `Chipset.csv` for a row whose token
   column matches `AGA` (case-insensitive).
3. **Numeric ID returned** - If found, the row's numeric ID (column 1) is used.
   For `AGA` this is `3` (row 3 in `Chipset.csv`).
4. **Stored as `uint16_t`** - The resolved ID is stored in the `include_ids[]` array and
   entered into the `rank_by_id[]` lookup table at the position of its low byte.

During scoring the same lookup process runs on the TLV variant's token data so both sides
map to the same numeric ID, allowing reliable comparison.

### CSV File Format

Each definition CSV in `assets_raw/defs/` follows the same layout:

```
<numeric_id>,<token>,<description>[,default]
```

Examples:

**Chipset.csv**
```
1,OCS,Original Chip Set,default
2,ECS,Enhanced Chip Set
3,AGA,Advanced Graphics Architecture
4,CD32,CD32 version
5,CDTV,CDTV version
```

**Language.csv**
```
1,Cz,Czech
2,Dk,Danish
3,NL,Dutch
4,En,English,default
5,Fi,Finnish
...
```

The optional `default` column marks the token that is assumed when a variant has no value
for this field at all (see Default Token Handling below).

---

## What Happens When a Filter Does Not Exist

If you write `[Filter.fakechipset]` but `fakechipset` is not a registered field name:

1. `field_registry_get_id(reg, "fakechipset")` returns `0`.
2. The loader skips the entire section.
3. `ProfileMeta.had_warnings` is set to `true`.
4. A debug log line is emitted if `debug=1` is set.
5. **The profile continues to load normally** - no crash, no abort.

The same behaviour applies to unknown weight keys such as `weight.fakechipset`.

In summary: an unrecognised filter section is silently ignored.  It does not cause the
profile to fail or fall back to defaults.

---

## What Happens When a Token Is Not in the CSV

If you write `include=FAST8M,FAST4M` but `FAST8M` does not exist in `Memory.csv`:

1. `csv_cache_lookup()` returns `0` (not found).
2. The loader falls back to computing a **FNV-1a 8-bit hash** of the token string
   (lowercased) and stores that hash as the token's ID.
3. The token is still added to the `include_ids[]` / `exclude_ids[]` list.
4. During scoring, variant tokens that also failed CSV lookup are hashed the same way, so
   hash-matched tokens can still score against each other.

**Implication:** a typo in a token name (`AGG` instead of `AGA`) will not be rejected at
load time.  It will simply hash to a different value and never match any real variant token,
so that filter entry has no practical effect.  No warning is currently emitted for
unresolved tokens.

---

## Scoring Engine

### How Scores Are Calculated

For each variant in the TLV data the engine iterates over every active `FP_FieldProfile`
entry:

```
for each field in profile:
    if variant has a token for this field:
        if token is in exclude list -> REJECT (score = 0, skip variant)
        if token is in include list -> rank = position in include list
        field_score = (include_count - rank) * weight
        total += field_score
    else:
        check default token (see below)

total += variant.interior_fields  (interior field bonus)
```

- **Rank 0** (first token in the include list) gives the highest field score.
- **Rank N-1** (last token) gives a score of `1 * weight`.
- A token not in the include list scores `0` for that field but does not reject the
  variant unless it also appears in the exclude list.
- **Exclusion is absolute**: a single excluded token causes the whole variant to be skipped
  regardless of its score on other fields.

### Priority Ordering in Include Lists

The order of tokens in the include list encodes preference:

```ini
include=AGA,ECS,OCS
```

| Token | Rank | Field score (weight=150, count=3) |
|-------|------|-----------------------------------|
| AGA | 0 | (3-0) * 150 = 450 |
| ECS | 1 | (3-1) * 150 = 300 |
| OCS | 2 | (3-2) * 150 = 150 |

An AGA variant therefore outscores an ECS variant by 150 points.

### Multi-Field Scoring

When multiple fields are configured, scores accumulate:

```
total = (chipset_field_score * weight.chipset)
      + (language_field_score * weight.language)
      + (memory_field_score * weight.memory)
      + interior_fields_bonus
```

The variant with the highest total score wins and is selected as the active variant for
that game.

### Interior Fields Bonus

Every TLV variant records `interior_fields`, a count of how many metadata fields were
decoded from inside the filename.  This bonus is added unconditionally to reward more
information-rich variants when two otherwise-equal variants compete.

### Default Token Handling

If a variant has no token for a given field, the engine checks whether the CSV for that
field defines a **default** row (column 4 = `default` in the CSV).  If it does:

- The default token ID is used as if the variant had declared that value.
- If the default token is in the exclude list, the variant is rejected.
- If the default token is in the include list, it scores at its rank position.

This prevents games that omit a chipset tag (common for OCS-era titles) from being
unfairly penalised when OCS is the defined default for `Chipset.csv`.

### Weight = 0

Setting a field weight to `0` (or omitting the `weight.*` key) disables scoring for that
field.  Exclusion rules still apply; the field simply contributes no points to the total
score.

---

## Debug Mode

Set `debug=1` in the `[Profile]` section (or set `Filter.debug=enabled` in your prefs
file) to enable verbose console output during profile load and scoring:

```
FP DBG: field=chipset include[0]='AGA' id=0x0003 csv=3
FP DBG: field=chipset include[1]='ECS' id=0x0002 csv=2
FP SUMMARY: field=chipset id=4 weight=150 include=3 exclude=0 allow_multiple=no
FP HIT: variant=0 field_id=4 rank=0 add=450 eff=0x0003
FP MISS: variant=1 field_id=4 (no matching tokens)
```

---

## Fallback Behaviour

If a `.profile` file is parsed but contains **no** `[Filter.*]` sections (for example if
all filter sections used unrecognised field names), `filter_profile_load_defaults()` is
called automatically.  The defaults are:

| Field | Default weight |
|-------|---------------|
| chipset | 150 |
| language | 120 |
| memory | 100 |

Include/exclude lists in the defaults are read from the prefs system keys
`Filter.chipset.include`, `Filter.chipset.exclude`, etc.  If those prefs keys are also
absent, the include lists are empty (all values accepted, no exclusions).

---

## Adding a New Filter Field

To add support for a new filterable field (for example `video` if it were not already
present):

### Step 1 - Register the field in pack_types.ini

Add the field name to the `FieldList` of the relevant pack type:

```ini
[PackTypes]
1 = Games|Games|...|Game|...,video,...
```

### Step 2 - Create the CSV definition file

Create `assets_raw/defs/Video.csv`:

```
1,PAL,PAL 50Hz,default
2,NTSC,NTSC 60Hz
```

The filename must match the field name with the first letter capitalised (convention;
the actual mapping is done by the field registry based on the CSV filename registered
in pack_types.ini).

### Step 3 - Add the filter section to your profile

```ini
[Filter.video]
include=PAL
exclude=NTSC

[Scoring]
weight.video=80
```

### Step 4 - Rebuild

No code changes are needed.  The field registry is built dynamically at runtime from
`pack_types.ini`.  The profile loader will discover the new field name on its next run.

---

## Worked Example - chipset_aga_only.profile

```ini
# Simplified AGA-first chipset profile
[Profile]
id=chipset_aga_only
name=Chipset AGA Only
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.language]
include=EN
exclude=

[Filter.memory]
include=
exclude=

[Scoring]
weight.chipset=200
weight.language=0
weight.memory=0
```

**How this is evaluated:**

1. The loader looks up `chipset` in the field registry -> field id resolved, CSV is
   `Chipset.csv`.
2. Tokens `AGA`, `ECS`, `OCS` are looked up in `Chipset.csv`:
   - `AGA` -> row 3, stored id `3`, rank `0`
   - `ECS` -> row 2, stored id `2`, rank `1`
   - `OCS` -> row 1, stored id `1`, rank `2`
3. `language` -> field id resolved, CSV is `Language.csv`.  Token `EN` -> looked up
   (matches `En` case-insensitively), stored id `4`, rank `0`.
4. `memory` include list is empty; all memory values accepted, nothing excluded.
5. Weights: chipset=200, language=0, memory=0.  Only chipset contributes to score.
6. For a variant tagged `AGA`: score = (3-0) * 200 = 600.
7. For a variant tagged `ECS`: score = (3-1) * 200 = 400.
8. For a variant tagged `CDTV` (not in include list, not excluded): chipset score = 0.
9. If a `CD32` variant existed and `CD32` were added to the exclude list it would be
   rejected outright regardless of other scores.

---

## Summary of Edge Cases

| Situation | Behaviour |
|-----------|-----------|
| `[Filter.fakechipset]` - field not in registry | Section silently skipped; `had_warnings=true` |
| Token in include list not found in CSV | Hashed and stored; may still match if TLV token also hashed identically |
| Typo in token name | Never matches; no error emitted |
| `weight.fieldname` without a matching `[Filter.fieldname]` | Field created with no include/exclude lists; only exclusions matter (none here) |
| `[Filter.fieldname]` without a matching `weight.*` | Field active for exclusion only; contributes 0 points |
| No filter sections parse successfully | Automatic fallback to `filter_profile_load_defaults()` |
| Variant lacks a token for a filtered field | Default CSV token (if defined) used; otherwise scores 0 for that field |
| Variant's token matches an exclude entry | Variant immediately rejected (score = 0) |
| Two variants with identical scores | First-encountered variant wins (tie is not broken by rank) |
| `include_count` exceeds `FP_MAX_INCLUDE` (32) | Excess tokens discarded; `had_warnings=true` |
