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

## Stage 5 — Assemble the TLV Output

The individual records produced for each filename are assembled into a single TLV structure. Each
entry in the TLV holds a field identifier, a length, and the value — either a numeric token ID or
a short string depending on the field type.

After the per-filename metadata records are built, a single `archive_info` entry is appended to
each record. This field carries two archive-level facts sourced directly from the DAT `<rom />`
attributes rather than from filename parsing:

| Sub-field | Offset | Size | Encoding | Description |
|---|---|---|---|---|
| `archive_size_kib` | 0 | 4 bytes | uint32, big-endian | Archive byte count rounded up to KiB: `(size_bytes + 1023) / 1024` |
| `archive_crc32` | 4 | 4 bytes | uint32, big-endian | CRC-32 digest from the DAT `crc=` attribute |

Both values are written in big-endian (Motorola) byte order so that the Amiga reader can consume
them directly without byte-swapping. For example, an archive reported as 679,540 bytes with
CRC `4af2c824` produces the 8-byte payload `00 00 02 98 4A F2 C8 24`.

The `archive_info` field is registered in the field registry like any other dynamic field, so it
appears in the metadata map and can be located by name. It does not participate in CSV token
matching or variant ranking.

A metadata map is prepended to the TLV file. This map records the field names and their
corresponding numeric identifiers so that the Amiga reader does not need to have the field names
baked in at compile time. The reader simply consults the map at startup and then navigates the
data by ID.

Immediately after the field map, a second header record embeds the CRC-32 fingerprint of every
CSV file that contributed data to this build. Each fingerprint entry names the CSV and stores its
checksum. Together these two header records make the TLV file entirely self-describing: a reader
or validator can determine both what fields are present and whether the lookup tables that
produced them have since changed.

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
size and makes lookups on the Amiga trivially fast.

**All heavy work is on the host.** CSV loading, token resolution, and filename decoding all
happen on the host PC. The Amiga receives a pre-processed binary and does no parsing at runtime.

**Source data is fingerprinted.** Every CSV lookup table used during a build has its CRC-32
checksum stored inside the TLV output. If a CSV is later edited — a new publisher added, a
language code corrected — the checksum in any existing TLV will no longer match. A host-side
validator can detect the mismatch and prompt a rebuild before a stale index reaches the Amiga.
