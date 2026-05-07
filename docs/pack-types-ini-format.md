# pack_types.ini — Column Reference

Each entry under `[PackTypes]` maps a numeric ID to a pipe-delimited value string:

```
ID = DisplayName|Abbreviation|SearchQuerySnippet|DatName|FieldList
```

---

## Columns

### 1. `DisplayName`
Human-readable label for the pack type (e.g. `Games`, `Demos`). Used in log output and summaries. Max 64 printable ASCII characters.

### 2. `Abbreviation`
Short label used in compact display contexts (e.g. `Games beta`). Max 16 printable ASCII characters.

### 3. `SearchQuerySnippet`
URL-encoded string fragment used when constructing web search queries against the WHDLoad download site. Max 128 characters.

### 4. `DatName`
**Key matching column.** The stem used to identify which pack type a DAT file belongs to. At runtime, `main.c` strips everything from the first `(` onwards from the DAT filename and compares the result case-insensitively against this field.

- Max **4 characters** — constrained by Amiga filename limits (15 chars total, minus a hyphen and a 10-char date suffix).
- Example: `Games(19-05-2025).dat` → stem `Games` is compared against `Game`, `Demo`, `Mags`, etc.

### 5. `FieldList`
Comma-separated list of metadata fields to extract for this pack type. Each name corresponds to a CSV file in `assets_raw/defs/` and a field registered in the runtime field registry. The order here defines processing order.

- Example: `sps,publisher,version,chipset,contributors,...`
- Fields not listed are ignored for that pack type.
- Field attributes (e.g. `allow_multiple`, prescan behaviour) are configured separately under `[FieldAttributes]`.

---

## Example

```ini
1 = Games|Games|Commodore%20Amiga%20-%20WHDLoad%20-%20Games%20(|Game|sps,publisher,version,chipset,contributors,crack_groups,disks,language,media,memory,software_houses,video,cover_disks,compilations,variant_tags,archive_info
```

| Column | Value |
|---|---|
| ID | `1` |
| DisplayName | `Games` |
| Abbreviation | `Games` |
| SearchQuerySnippet | `Commodore%20Amiga%20-%20WHDLoad%20-%20Games%20(` |
| DatName | `Game` |
| FieldList | `sps,publisher,version,...` |

---

## FieldAttributes Section

The `[FieldAttributes]` section configures per-field behaviour independently of pack type:

- `<field>.allow_multiple = true` — the field can match more than one value per filename.
- `<field>.prescan.enabled = true` — the field is extracted in a pre-pass before main tokenisation. Useful for multi-word tokens that would otherwise be split.
- `<field>.prescan.order` — lower numbers run first in the prescan pass.
- `<field>.prescan.remove_from_filename = true` — matched tokens are stripped from the working filename before the main token loop runs.
