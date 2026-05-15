# TLV Creation Pipeline — Executive Overview

This document describes how the dat_to_tlv tool converts a WHDLoad DAT file into a binary TLV
file suitable for use on an Amiga.

---

## Purpose

The Amiga needs a compact, fast-to-read binary index of WHDLoad game metadata. The TLV format
(Tag-Length-Value) provides exactly that: a single file the Amiga can scan without a database
engine, CSV parser, or internet connection.

The pipeline takes a standard Logiqx-style DAT file as input and produces that binary index as
output. All field definitions and lookup tables come from configuration and CSV files on the host
PC at build time.

---

## Stage 1 — Determine What Fields Are Needed

Before any processing begins, the tool reads `pack_types.ini` to understand what kind of data it
is working with.

The INI file defines a small number of pack types (Games, Demos, Magazines, etc.). Each pack type
specifies the exact set of metadata fields that are relevant to entries of that type. For example,
a Games pack cares about chipset, language, number of disks, memory requirements, and so on. A
Demos pack cares about a different set.

The tool inspects the DAT filename to identify which pack type applies, then reads that pack
type's field list. This field list is the authoritative definition of what columns the TLV output
will contain. Nothing outside this list is included; nothing inside this list is skipped.

---

## Stage 2 — Load the Lookup Tables

Each field named in the pack type's field list corresponds to a CSV file that acts as a lookup
table. For example, the `language` field is backed by a language CSV, the `chipset` field by a
chipset CSV, and so on.

The tool loads all relevant CSV files into memory at startup. Each CSV maps human-readable token
strings to compact numeric identifiers. When a token from a game's filename is found in a CSV, its
numeric ID is stored instead of the full string. This is what keeps the TLV file small enough for
the Amiga to handle efficiently.

As each CSV is loaded, a CRC-32 checksum is computed over its raw file content. These checksums
are carried forward and embedded in the finished TLV file alongside the data they produced. This
creates a permanent record of exactly which version of each lookup table was used to build the
index.

Some fields have no CSV backing — they are either free-form values (such as version numbers) or
are derived directly from the filename without a lookup step.

---

### CSV Alias Rows

A CSV file may contain more than one row for the same numeric ID. The **first** row encountered
for a given ID is the **canonical** row: its token and description are used whenever the pipeline
maps that ID back to a human-readable label (for example, in CSV export reports or the
effective-column output). Every subsequent row with the same ID is an **alias** row.

Alias rows extend the set of filename tokens that resolve to a single ID — useful when a field
value appears under several spellings in real-world filenames. For example:

```
7,UNKNOWN512K,512 KB memory (type unknown),default
7,512k,Alias for UNKNOWN512K
7,512kb,Alias for UNKNOWN512K
```

All three tokens forward-resolve to ID 7. Reverse-resolution (ID → label) always returns
`UNKNOWN512K` and its description because that is the first row seen for ID 7.

**Rules that apply to alias rows:**

- Alias rows participate fully in forward (token → ID) lookup. All spellings match equally.
- Alias rows are invisible to reverse (ID → token/description) lookup. Only the canonical row
  is returned.
- The `default` marker in the optional fourth column is only meaningful on the canonical row.
  An alias row carrying `default` is treated as an extra default declaration, which the report
  tool flags as ambiguous (more than one default row).
- Duplicate token strings within the same CSV file are still rejected regardless of the ID.
  The second occurrence of the same token (case-insensitive) is silently dropped.
- CRC-32 fingerprinting is not affected. The checksum is computed over the raw file bytes,
  so any change to the file content changes the fingerprint as normal.

---

## Stage 3 — Parse the DAT File

The tool reads the DAT file and extracts every `<rom ... />` entry it contains. For each entry
it captures three attributes:

- **`name`** — the archive filename, which carries embedded metadata as structured tokens.
- **`size`** — the archive byte count, recorded verbatim from the DAT.
- **`crc`** — the archive CRC-32 digest, recorded verbatim from the DAT as a hex string.

The `name` attribute drives all metadata decoding in Stage 4. The `size` and `crc` attributes
are carried forward unchanged and written into the TLV as archive transport facts in Stage 5.

If a `size` or `crc` attribute is absent or malformed the field is zeroed and a warning is
emitted. Processing continues normally — a missing checksum or size does not abort the run.

The tool does not use the DAT file for metadata beyond these three attributes. All semantic
metadata (chipset, language, disks, etc.) comes from decoding the archive filename in Stage 4.

---

## Stage 4 — Decode Each Filename

Each filename is processed individually. The decoding pass works in two phases.

**Pre-scan:** Certain high-priority fields, such as contributor credits and variant tags, are
identified and extracted first before the main pass runs. This prevents their tokens from being
misidentified as other fields during the general scan.

**Main scan:** The remaining filename tokens are matched against the loaded CSV tables for each
field in the pack type's field list. When a token matches a known entry in a CSV, the
corresponding numeric ID is recorded. Fields are tested in an order designed to resolve the most
common matches first, minimising unnecessary lookups.

At the end of this process each filename has been converted into a structured set of field-ID and
value pairs.

---

### Compact Multilanguage Token Rule

Language tokens in WHDLoad filenames appear in two forms:

**Single-language token** — exactly two characters matching a Language.csv entry, for example
`_De_` (German) or `_En_` (English). The token is accepted as a language if and only if the
whole two-character token is present in Language.csv.

**Compact multilingual token** — a run of two-character language codes joined without separators,
for example `DeEsFrIt` (German, Spanish, French, Italian). A token is accepted as a compact
multilingual sequence if and only if:

1. the token length is even and at least 4 characters;
2. the token can be split cleanly into 2-character chunks with no remainder;
3. every 2-character chunk resolves to a known entry in Language.csv;
4. no unmatched characters remain.

If any chunk is absent from Language.csv the **whole token is rejected** as a language token.
Partial matches are not accepted — this prevents arbitrary tokens from yielding embedded false
language codes.

**Examples:**

| Token | Outcome | Reason |
|---|---|---|
| `De` | German | exact single-code match |
| `En` | English | exact single-code match |
| `DeEsFrIt` | De; Es; Fr; It | all four 2-char chunks are in Language.csv |
| `DeFr` | De; Fr | both chunks resolve |
| `EasyPlay` | *rejected* | chunk `Ea` is not in Language.csv |
| `Infogrames` | *rejected* | chunk `In` is not in Language.csv |
| `DeXxFr` | *rejected* | chunk `Xx` is not in Language.csv; De and Fr are not extracted |
| `EnFrX` | *rejected* | odd length (5); cannot be split into 2-char chunks |

This rule is implemented in `filename_processor.c` by the static helper
`is_compact_language_token`, which is called from `language_parser_parse_token`. The function
short-circuits immediately on the first unknown chunk, so no unnecessary lookups are made.

---

## Stage 5 — Assemble the TLV Output

The individual records produced for each filename are assembled into a single TLV structure. Each
entry in the TLV holds a field identifier, a length, and the value — either a numeric token ID or
a short string depending on the field type.

After the per-filename metadata records are built, two additional fields are injected into each
record:

**`group_id`** (field ID 0x05) — a uint16, big-endian, assigned by the builder to every variant
that shares the same logical game title. All variants of *1869* get the same group_id; all
variants of *Zynaps* get a different one. The builder derives the canonical group name by scanning
the display name for the `_v<digit>` pattern (e.g. `1869_v1.2_De` → `1869`) and assigns
numeric IDs sequentially starting at 1. A group_id value of 0 is a sentinel meaning absent.
This field does not participate in CSV token matching or variant ranking.

**`archive_info`** — a single field carrying two archive-level facts sourced from the DAT
`<rom />` attributes:

| Sub-field | Offset | Size | Encoding | Description |
|---|---|---|---|---|
| `archive_size_kib` | 0 | 4 bytes | uint32, big-endian | Archive byte count rounded up to KiB: `(size_bytes + 1023) / 1024` |
| `archive_crc32` | 4 | 4 bytes | uint32, big-endian | CRC-32 digest from the DAT `crc=` attribute |

Both values are written in big-endian (Motorola) byte order so that the Amiga reader can consume
them directly without byte-swapping. For example, an archive reported as 679,540 bytes with
CRC `4af2c824` produces the 8-byte payload `00 00 02 98 4A F2 C8 24`.

The `archive_info` field is registered in the field registry like any other dynamic field, so it
appears in the field map and can be located by name. It does not participate in CSV token matching
or variant ranking.

Three header blocks are prepended to the TLV file:

**Block 0x01 — Field map.** Records field names and their numeric identifiers so that the Amiga
reader does not need the field names baked in at compile time. The reader consults the map at
startup and navigates the data by ID. The two implicit fields always occupy the first two slots:
`display_name` = 0x04 (the record boundary marker) and `group_id` = 0x05. Pack-type fields begin
at 0x06.

**Block 0x02 — Group map.** Written immediately after the field map. Stores a compact table that
maps each numeric group_id to its canonical group name (the game title). Wire format:
`[1 byte 0x02][2 bytes LE payload_size][2 bytes LE group_count]` then per entry:
`[2 bytes BE group_id][1 byte name_len][name_len bytes group_name (no NUL)]`. The runtime reads
this block and uses it for grouping without touching the variant records. Old TLVs without block
0x02 are handled gracefully: the runtime falls back to heuristic group derivation on the
display_name field.

**Block 0x04 — CSV fingerprints.** Embeds the CRC-32 checksum of every CSV file used during the
build. Each entry names the CSV and stores its checksum. Together the three header blocks make the
TLV file entirely self-describing: a reader can determine what fields are present, how variants
are grouped, and whether the lookup tables have since changed.

---

## Stage 6 — Write the Output File

The completed TLV structure is written to disk as a single binary file. This file is the
deliverable — ready to be copied to the Amiga and used by the runtime reader.

A summary log is also written, recording the number of entries processed, success and error
counts, and timing information for the build and save stages.

---

## Key Design Decisions

**Pack type drives the schema.** The field list in `pack_types.ini` is the sole definition of
what the TLV contains for a given pack. Changing what fields are included is a configuration
change, not a code change.

**Metadata is self-describing.** The TLV file carries its own field map, so the Amiga reader
remains independent of the specific field set chosen at build time. New fields can be added
without modifying the Amiga binary.

**Tokens, not strings.** Wherever a field has a finite set of known values (chipset, language,
media type, etc.), the TLV stores a numeric token ID rather than the string. This reduces file
size and makes lookups on the Amiga trivially fast. A single numeric ID can be reached by
multiple alias tokens (see *CSV Alias Rows* in Stage 2); only the canonical first row for that
ID is used when converting the ID back to a display label.

**All heavy work is on the host.** CSV loading, token resolution, and filename decoding all
happen on the host PC. The Amiga receives a pre-processed binary and does no parsing at runtime.

**Source data is fingerprinted.** Every CSV lookup table used during a build has its CRC-32
checksum stored inside the TLV output (block 0x04). If a CSV is later edited — a new publisher
added, a language code corrected — the checksum in any existing TLV will no longer match. A
host-side validator can detect the mismatch and prompt a rebuild before a stale index reaches the
Amiga.

**Variants are pre-grouped.** The builder assigns a numeric `group_id` to every variant and
stores a group_id → group_name map in TLV header block 0x02. The runtime reads this map once at
load time and can group variants by integer comparison rather than by string parsing. Old TLVs
without block 0x02 remain readable; the runtime falls back to heuristic display_name splitting.
