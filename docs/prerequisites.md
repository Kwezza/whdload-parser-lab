# Prerequisites — Input Data Conventions

This file documents what the pipeline expects from its input data. It will grow as additional input requirements are established.

---

## DAT Files

### Source and Format

DAT files are Logiqx-format XML files distributed by the No-Intro / WHDLoad preservation community. Each file covers one pack type (Games, Demos, etc.) and contains one `<rom>` element per archived package, carrying the filename, byte size, and CRC-32.

```xml
<rom name="1869_v1.0_AGA.lha" size="1839850" crc="505be129" ... />
```

The pipeline reads only the `name`, `size`, and `crc` attributes. Everything else in the XML is ignored.

### Raw Filename from the ZIP

When a DAT ZIP is downloaded and extracted, the file inside uses a long descriptive name following the No-Intro naming convention:

```
Commodore Amiga - WHDLoad - <PackName> (<YYYY-MM-DD>).dat
Commodore Amiga - WHDLoad - <PackName> - Beta & Unofficial (<YYYY-MM-DD>).dat
```

Examples from `assets_raw/Dats/`:

| Raw extracted filename | Pack |
|---|---|
| `Commodore Amiga - WHDLoad - Games (2026-04-17).dat` | Games |
| `Commodore Amiga - WHDLoad - Games - Beta & Unofficial (2026-04-26).dat` | Games Beta |
| `Commodore Amiga - WHDLoad - Demos (2026-03-23).dat` | Demos |
| `Commodore Amiga - WHDLoad - Demos - Beta & Unofficial (2026-04-20).dat` | Demos Beta |
| `Commodore Amiga - WHDLoad - Magazines (2025-07-24).dat` | Magazines |

### Required Rename Before Use

The pipeline **cannot use the raw extracted filename directly**. The stem extraction logic in `main.c` (`get_dat_stem`) takes the filename, strips the directory prefix, then discards everything from the first `(` onwards — including the date — to produce a short stem.

That stem is then compared case-insensitively against the `DatName` column in `pack_types.ini` (max 4 characters).

**The raw filenames are too long to produce a stem that matches any `DatName` entry.** The files must be renamed to a short form before being passed to the converter:

```
<DatName>(<YYYY-MM-DD>).dat
```

| Renamed filename | Stem extracted | DatName in INI | Pack type matched |
|---|---|---|---|
| `Game(2026-04-17).dat` | `Game` | `Game` | Games (ID 1) |
| `Demo(2026-03-23).dat` | `Demo` | `Demo` | Demos (ID 2) |
| `Mags(2025-07-24).dat` | `Mags` | `Mags` | Magazines (ID 3) |
| `GamB(2026-04-26).dat` | `GamB` | `GamB` | Games Beta (ID 4) |
| `DemB(2026-04-20).dat` | `DemB` | `DemB` | Demos Beta (ID 5) |

> The `(date)` portion after the stem is required to be present so the stem boundary is correctly identified. Without it the entire base filename (minus extension) becomes the stem.

### Stem Extraction Rules

`get_dat_stem()` in `app_src/main.c`:

1. Strips the directory path (last `/` or `\`).
2. Takes everything up to (but not including) the first `(`.
3. If no `(` exists, falls back to everything before the last `.`.
4. The result is trimmed to 63 characters maximum internally, but `DatName` entries in the INI are validated to **4 characters maximum** (Amiga filename constraint: 15 chars total minus a hyphen and a 10-char date suffix).

### Fallback Behaviour

If no `DatName` entry matches the extracted stem, `find_pack_index_for_dat()` returns index `0`, which is whichever pack type appears first in `pack_types.ini` (currently Games, ID 1). There is no error or warning — the file is silently processed as that pack type.

---

## Txt Files (Name Lists)

The `assets_raw/Dats/` folder also contains `.txt` files that are tab-delimited name/size/crc extracts of the same data:

```
1000ccTurbo_v1.0.lha	207532	33067126
```

These are not used by the current pipeline. The pipeline always reads from the `.dat` XML directly via `parse_dat_entries_minimal()`.

---

## pack_types.ini

See [pack-types-ini-format.md](pack-types-ini-format.md) for the full column reference.

The INI must be present at the path given to the converter (default: `assets_raw/prefs/pack_types.ini`). Every `DatName` value must be 1–4 uppercase-or-lowercase ASCII alphanumeric characters. The loader validates this strictly and exits with an error if a value exceeds the limit.
