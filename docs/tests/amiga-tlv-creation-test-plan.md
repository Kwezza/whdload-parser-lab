# Amiga TLV Creation Validation Plan

**Created:** 2026-05-27
**Status:** Phase 4 complete — all pass criteria met

| Phase | Status | Date |
|---|---|---|
| Pre-condition (`__stack`) | PASSED | 2026-05-27 |
| Phase 1 — Build Amiga binary | PASSED | 2026-05-27 |
| Phase 2 — Assemble bundle | PASSED | 2026-05-27 |
| Phase 3 — Amiga device run | PASSED | 2026-05-27 |
| Phase 4 — Binary comparison | PASSED | 2026-05-27 |

---

## Purpose

The endianness remediation (Items 1–12 in
[endianness-divergence.md](../endianness-divergence.md)) has been completed and the
Amiga *reading* path validated end-to-end via `test_amiga_endian` (see
[amiga-endian-crossval-plan.md](amiga-endian-crossval-plan.md)).

This plan validates the complementary side: the Amiga `dat_to_tlv` *writing* pipeline.
It confirms that the Amiga binary reads DAT files and produces TLV files that are
byte-for-byte identical to the PC-generated baseline.  Since both now write the same
uniform big-endian format, binary file equality is the natural oracle.

---

## What the Test Must Prove

| Risk | How detected |
|---|---|
| TLV block framing written as LE by Amiga | `fc /b` byte mismatch in block header region |
| Token ID fields written as LE by Amiga | `fc /b` byte mismatch in data record region |
| Binary crashes before completing | No output TLV files written; error printed to console |
| Stack overflow in creation pipeline | Immediate crash (HALT3 / guru) on first DAT parse |

---

## Known Pre-condition: `__stack` Override

`tools_src/dat_to_tlv_main.c` contains:

```c
#if PLATFORM_AMIGA
unsigned long __stack = 131072UL;
#endif
```

This instructs vbcc to set the Amiga stack to 128 KB at binary load time — before
`main()` is entered.  This is the mechanism that was *absent* in the filter facade
binary and caused the HALT3 crash during Phase 4 of the endian cross-validation.
Verify it is present in the source before building.

---

## Phase 1 — Build the Amiga binary

Run:

```bat
make TARGET=amiga run
```

> **Note:** The Makefile default target is `help`; use `run` (which depends on `$(BIN)`)
> to trigger the Amiga build.  The `make amiga` shortcut listed in the help text is not
> defined.

Expected output: `build\amiga\dat_to_tlv` produced, zero vbcc errors, zero warnings.

**Gate:** `build\amiga\dat_to_tlv` exists.

**Result (2026-05-27): PASSED.** Binary linked with zero errors and zero warnings.
`__stack = 131072UL` confirmed present in `tools_src/dat_to_tlv_main.c` line 23.

---

## Phase 2 — Assemble the bundle

Run `assemble_tlv_build_bundle.bat` (created alongside this plan).  It produces
`amiga_tlv_bundle\` with the following structure:

```
amiga_tlv_bundle\
  dat_to_tlv                        <- Amiga binary (build\amiga\dat_to_tlv)
  assets_raw\
    Dats\
      DemB(2026-04-20).dat
      Demo(2026-03-23).dat
      GamB(2026-04-26).dat
      Game(2026-04-17).dat
      Mags(2025-07-24).dat
    defs\                           <- all CSV files (CRLF preserved, binary copy)
    prefs\
      pack_types.ini
  output\                           <- empty; dat_to_tlv writes here
```

All copies use `/B` (binary mode) to preserve CRLF line endings in the CSV files.
The CRC validator embedded in the binary expects CRLF; do not convert line endings.

**Gate:** `amiga_tlv_bundle\` contains the binary and all five DAT files.

**Result (2026-05-27): PASSED.** All five DAT files confirmed present in
`amiga_tlv_bundle\assets_raw\Dats\`.

---

## Phase 3 — Amiga device run

Mount `amiga_tlv_bundle\` in WinUAE as a read-write directory hard drive (DH1:).

```
WinUAE: CD & Hard drives > Add Directory or Archive
  Path:      <full path>\amiga_tlv_bundle
  Label:     TLVBLD    Device: DH1    Read-Write: yes
```

From the Amiga CLI:

```
STACK 100000
cd DH1:
dat_to_tlv
```

The `__stack = 131072UL` override sets the stack to 128 KB at load time;
`STACK 100000` at the CLI raises the parent shell stack as an additional safety margin.

### Expected console output

The binary processes all five pack types in sequence and prints a per-pack summary.
Expected variant counts (from the PC run in Item 11 of the remediation):

```
DemB: 12 variants processed, 0 errors
Demo: 904 variants processed, 0 errors
GamB: 128 variants processed, 0 errors
Game: 3973 variants processed, 0 errors
Mags: 104 variants processed, 0 errors
```

If the binary crashes immediately (HALT3 / guru meditation) before printing anything,
the most likely cause is a large stack-allocated local in the creation pipeline — the
same class of bug fixed in the filter facade (see Phase 4 of
[amiga-endian-crossval-plan.md](amiga-endian-crossval-plan.md)).  Record the crash
address and check `tlv_builder.c`, `filename_processor.c`, and
`whdtlv_integration.c` for large local structs or arrays.

**Gate:** Binary runs to completion with 0 errors for all five pack types.

**Result (2026-05-27): PASSED.**  Actual console output (captured in `output.txt`):

```
DemB:  12 entries, 12 processed, 12 successful, 0 errors — 52 TLV entries
Demo: 904 entries, 904 processed, 904 successful, 0 errors — 4471 TLV entries
GamB: 128 entries, 128 processed, 128 successful, 0 errors — 582 TLV entries
Game: 3973 entries, 3973 processed, 3973 successful, 0 errors — 19896 TLV entries
Mags: 104 entries, 104 processed, 104 successful, 0 errors — 521 TLV entries
```

No crash, no HALT3.  `PROGDIR:benchmark-summary.txt` in the log confirms this was an
AmigaOS run, not the host binary.



---

## Phase 4 — Binary comparison on PC

After the Amiga run the five TLV files are in `amiga_tlv_bundle\output\`, accessible
from the PC via the shared WinUAE directory.  Run `fc /b` against the PC-generated
baseline in `output\`:

```bat
fc /b "output\DemB(2026-04-20).tlv"  "amiga_tlv_bundle\output\DemB(2026-04-20).tlv"
fc /b "output\Demo(2026-03-23).tlv"  "amiga_tlv_bundle\output\Demo(2026-03-23).tlv"
fc /b "output\GamB(2026-04-26).tlv"  "amiga_tlv_bundle\output\GamB(2026-04-26).tlv"
fc /b "output\Game(2026-04-17).tlv"  "amiga_tlv_bundle\output\Game(2026-04-17).tlv"
fc /b "output\Mags(2025-07-24).tlv"  "amiga_tlv_bundle\output\Mags(2025-07-24).tlv"
```

`fc /b` exits with code 0 ("no differences encountered") when files are identical and
non-zero when they differ.

**Result (2026-05-27): PASSED.** All five files are byte-for-byte identical:

```
IDENTICAL  DemB(2026-04-20)
IDENTICAL  Demo(2026-03-23)
IDENTICAL  GamB(2026-04-26)
IDENTICAL  Game(2026-04-17)
IDENTICAL  Mags(2025-07-24)
```

### If differences are found

`fc /b` prints the first differing byte offset and the two byte values.  Use that
offset to locate the affected TLV field:

- Offset in block `0x01` / `0x02` / `0x04` region — block framing write path
  (`tlv_write_metadata_map`, `tlv_write_group_map`, `tlv_write_csv_fingerprints` in
  `src/whdtlv/core/tlv_builder.c`)
- Offset in data record region — either `value_length` or a token ID field value
  (`tlv_write_record_to_file` or a `filename_processor.c` BE-encode site)

The remediation confirmed all write paths use `write_u16_be` / `write_u32_be` on the
host; any difference would indicate a vbcc-specific code-generation issue or a missed
platform-guard path.

**Gate:** All five `fc /b` comparisons exit with "no differences encountered".

---

## Phase 5 — Filter validation against Amiga-generated TLVs (optional)

If Phase 4 passes, optionally replace the `assets_raw/TLV/` fixture TLVs with the
Amiga-generated ones and re-run the full test suite:

```bat
copy /B "amiga_tlv_bundle\output\Game(2026-04-17).tlv" "assets_raw\TLV\Game"
copy /B "amiga_tlv_bundle\output\Mags(2025-07-24).tlv" "assets_raw\TLV\Mags"
make test-filter
make test-report
make test-amiga-endian
```

If all three test targets pass with the same counts as before (34/0, 56/0), the
Amiga-generated TLVs are not only byte-identical to the PC baseline but also fully
verified through the filter and report pipelines.

Restore the original fixtures afterwards (they are the PC-generated baseline):

```bat
copy /B "output\Game(2026-04-17).tlv" "assets_raw\TLV\Game"
copy /B "output\Mags(2025-07-24).tlv" "assets_raw\TLV\Mags"
```

**Gate (optional):** All three test targets pass unchanged.

---

## Pass Criteria

The Amiga TLV creation validation is considered **complete** when:

1. `build\amiga\dat_to_tlv` runs on the Amiga with 0 errors across all five pack types.
2. All five `fc /b` comparisons exit with "no differences encountered".

Phase 5 is confirmatory and optional.

---

## Relation to Existing Tests

This plan is complementary to
[amiga-endian-crossval-plan.md](amiga-endian-crossval-plan.md), which validated the
Amiga *reading* path.  Together they close the full round-trip:

```
PC DAT files -> Amiga dat_to_tlv -> TLV files == PC-generated TLV files
                                               |
                                               v
                                    Amiga test_amiga_endian -> 34/0 pass
```
