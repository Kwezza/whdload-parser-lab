# dat_to_tlv — Executive Overview

**Audience:** Amiga enthusiasts and WHDLoad users. No programming knowledge is assumed.

---

## The Variant Problem

WHDLoad is a system that lets you run classic Amiga software from hard disk without swapping
floppies. It works by wrapping each original game in a small loader program, and the broader
WHDLoad community maintains a catalogue of those loaders — one for almost every game ever
released on the Amiga.

The problem is that a single game very often exists in multiple editions. A game popular enough
to be sold across Europe might appear as any combination of:

- **Chipset variants** — OCS (original hardware), ECS (enhanced), AGA (A1200/A4000), CD32, CDTV
- **Video standard** — PAL or NTSC
- **Language** — English, French, German, Italian, Spanish, and others
- **Memory requirements** — some builds need extra fast RAM; lower-memory editions exist for
  stock machines
- **Special editions** — censored or uncensored versions, low-memory builds, enhanced editions,
  competition versions

This is not unusual — a popular title may have ten or more archive variants in the catalogue.

If a downloader fetches every archive that matches a game title, the user ends up with a bloated
collection full of editions that will not run well (or at all) on their Amiga. A French user with
an A1200 (AGA chipset) and 4 MB of fast RAM wants the AGA French edition where one exists — and
a sensible English AGA fallback where it does not.

---

## What dat_to_tlv Does

`dat_to_tlv` is normally run on a host PC before the Amiga uses the finished index. It solves this variant problem in two stages.

### Stage 1 — Build the Index

The tool reads a Logiqx-style DAT catalogue file (the kind published by WHDLoad and maintained by
cataloguing projects). For each archive entry in that DAT it:

1. Parses the archive filename to extract embedded metadata — chipset, language, disk count,
   memory requirements, and other fields.
2. Validates each piece of metadata against lookup tables (CSV files) that define the recognised
   values.
3. Groups variants of the same title together under a common group name.
4. Writes everything into a single compact binary file in TLV format.

The result is a small, fast-to-read index file — typically a fraction of a megabyte — that an
Amiga can scan without needing a database engine, an XML parser, or an internet connection.

### Stage 2 — Filter and Select

At run time (on the Amiga or on the host PC) the filter system reads the TLV index and applies a
**profile** — a short configuration file that describes your machine. The profile tells the engine
which chipset you have, which language you prefer, how much RAM your machine has, and how strongly
to prefer each criterion. The engine scores every variant of each title and picks the best match.

By default the result is one archive per title, chosen for your specific hardware — no
unnecessary duplicate editions, and far fewer unsuitable variants. Advanced profiles can
deliberately select more than one archive per title, for example one AGA choice and one
OCS/ECS fallback.

---

## No Compiler Required

The key design decision in `dat_to_tlv` is that almost all behaviour is driven by plain text
files that anyone can edit with a text editor. You do not need to touch source code or recompile
anything to:

- Add a new recognised token to an existing metadata field (edit a CSV file)
- Define a new metadata field entirely (add a name to the INI and create a CSV)
- Add a new pack type — for example, a new category of WHDLoad software (add an entry to the INI)
- Create or tweak a filter profile for your machine (write or copy a `.profile` file)

Changes to CSV files or `pack_types.ini` take effect after you re-run `dat_to_tlv` to rebuild
the TLV index. Changes to a `.profile` file take effect immediately — the filter reads the
profile at run time, so no index rebuild is needed.

The configuration files live in [assets_raw/prefs/](../../../assets_raw/prefs/) and
[assets_raw/defs/](../../../assets_raw/defs/). The filter profiles live in
[assets_raw/profiles/](../../../assets_raw/profiles/).

---

## What You Can Change

### Token Lookup Tables — CSV Files

Most metadata fields are backed by a CSV file in [assets_raw/defs/](../../../assets_raw/defs/).
Each row maps a token (the short code that appears in archive filenames) to a numeric ID and a
human-readable description. Some fields — such as version strings or publisher-style free-text
values — are stored differently because they are not simple fixed-token lists.

For example, [assets_raw/defs/Chipset.csv](../../../assets_raw/defs/Chipset.csv) contains:

```
1,OCS,Original Chip Set,default
2,ECS,Enhanced Chip Set
3,AGA,Advanced Graphics Architecture
4,CD32,CD32 version
5,CDTV,CDTV version
```

If a new chipset variant were to appear in future WHDLoad archives (unlikely, but illustrative),
you would add one row to this CSV, then re-run `dat_to_tlv` to rebuild the index. The program
itself does not need to change.

You can also add **alias rows** — extra rows for the same numeric ID that let the parser recognise
alternative spellings of the same value that appear in real-world filenames.

The TLV also records fingerprints of the CSV lookup tables used to build it. If those CSV files
are later changed, the filter can detect that the index is stale and ask for it to be rebuilt
rather than silently using mismatched definitions.

### Pack Type and Field Definitions — pack_types.ini

[assets_raw/prefs/pack_types.ini](../../../assets_raw/prefs/pack_types.ini) defines:

- **Pack types** — the broad categories of WHDLoad software (Games, Demos, Magazines, etc.). Each
  entry has a short name that is matched against the DAT filename, plus the list of metadata
  fields that are relevant for that category.
- **Field attributes** — per-field behaviour such as whether a field can hold more than one value
  per archive, and whether it is extracted in a pre-scan pass before main tokenisation.

For the full column reference, see [docs/pack-types-ini-format.md](../../pack-types-ini-format.md).

### Filter Profiles — .profile Files

A profile is a plain text INI-style file in [assets_raw/profiles/](../../../assets_raw/profiles/).
It tells the filter engine what your machine looks like and how strongly to prefer each
characteristic.

Here is the built-in PAL AGA 4 MB profile as a concrete example
([assets_raw/profiles/pal_aga_4mb.profile](../../../assets_raw/profiles/pal_aga_4mb.profile)):

```ini
[Profile]
id=pal_aga_4mb
name=PAL AGA 4MB Default

[Filter.chipset]
include=AGA,ECS,OCS
exclude=PALNTSC

[Filter.language]
include=EN,DE
exclude=

[Filter.memory]
include=FAST8M,FAST4M,FAST2M,FAST1M
exclude=SLOW256K

[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
```

The `include=` list is a priority order — tokens listed earlier are preferred over tokens listed
later. The `weight.*` values control how strongly each field contributes to the overall score. A
chipset weight of 150 means chipset suitability matters more than language (120) or memory (100)
when choosing between two otherwise equal variants.

Some tokens exist because they appear in real WHDLoad archive names, even when their naming is
historically awkward — `PALNTSC` under `[Filter.chipset]` is one such case.

To create a profile for your own machine, copy an existing `.profile` file, give it a new `id`
and `name`, and adjust the include lists and weights to match your hardware.

For the full profile format reference, see [docs/profile_system.md](../../profile_system.md).

> **A note on complexity:** Simple token additions to an existing CSV are usually safe and
> straightforward. Adding new fields or entirely new pack types is more advanced — after making
> those changes, rebuild the TLV and check the report output to confirm everything was recognised
> as expected.

---

## Glossary

| Term | Meaning |
|------|---------|
| **DAT file** | A Logiqx-style XML catalogue that lists WHDLoad archives with their filenames, sizes, and CRC checksums. The standard input format for `dat_to_tlv`. |
| **TLV** | Tag-Length-Value. A compact binary encoding where each piece of data is stored as a small header (the tag identifying what the data is) followed by its length and its content. Designed to be parsed quickly with minimal memory. |
| **Token** | A short code embedded in an archive filename that carries a single piece of metadata. For example, `AGA` is a chipset token; `EN` is a language token. |
| **Field** | A named category of metadata — `chipset`, `language`, `memory`, `disks`, and so on. Each field is backed by a CSV lookup table that maps tokens to numeric IDs. |
| **Pack type** | A broad category of WHDLoad software — Games, Demos, Magazines — defined in `pack_types.ini`. Each pack type has its own field list because different categories care about different metadata. |
| **Profile** | A `.profile` configuration file that describes a specific Amiga configuration. The filter engine uses it to score and rank the variants of each title, then selects the best match. |
| **Group** | A set of archive variants that the tool has identified as different editions of the same underlying title. The filter engine works one group at a time and selects one winner per group by default; advanced profiles may select more than one. |
| **CRC-32** | A checksum used to verify file integrity. `dat_to_tlv` records the CRC of each CSV file it used so the finished TLV carries a permanent record of exactly which lookup table version produced it. |
| **Alias row** | An extra row in a CSV lookup table that gives an alternative token spelling for the same numeric ID. Alias rows let the parser recognise multiple real-world spellings of the same value without changing how it is stored or displayed. |
