# dat_to_tlv Standalone Tool

This folder contains a standalone DAT-to-TLV tool that can now be built for both host and Amiga targets using only the files staged inside `variant_backport_staging`.

## What It Does

`dat_to_tlv`:
- reads a Logiqx-style DAT file
- extracts each `<rom name="..."/>` filename
- parses each filename through the staged TLV filename pipeline
- writes one aggregate TLV output file

Current default input:
- `assets_raw/Games(19-05-2025).dat`

Current default output:
- `output/Games(19-05-2025).tlv`

## Build

From inside `variant_backport_staging`:

```bat
make
```

This builds the host version by default:

```text
build/host/dat_to_tlv.exe
```

Build the host version explicitly:

```bat
make TARGET=host
```

Build the Amiga version:

```bat
make TARGET=amiga
```

This builds:

```text
build/amiga/dat_to_tlv
```

Shortcut targets:

```bat
make host
make amiga
```

Show build help:

```bat
make help
```

To clean the host build and generated default TLV file:

```bat
make clean
```

## Run

From inside `variant_backport_staging`:

```bat
build\host\dat_to_tlv.exe
```

This uses the built-in defaults:
- DAT path: `assets_raw/Games(19-05-2025).dat`
- output path: `output/Games(19-05-2025).tlv`
- CSV defs path: `assets_raw/defs`
- pack types path: `assets_raw/prefs/pack_types.ini`

You can also run through make:

```bat
make run
```

For the Amiga target, the Makefile builds the binary but does not try to execute it on the host machine.

Important for Amiga runs:
- before starting `dat_to_tlv`, manually raise the stack, for example `STACK 100000`
- the tool now writes this same warning as the first runtime logfile note when logging is enabled

## Command-Line Usage

```text
dat_to_tlv[.exe] [--no-log] [dat_path output_path [csv_dir pack_types_ini]]
```

Command-line options:
- `--no-log` disables logfile creation and logfile writes for faster runs on Amiga and emulators

Examples:

Use defaults:

```bat
build\host\dat_to_tlv.exe
```

Specify DAT and output only:

```bat
build\host\dat_to_tlv.exe assets_raw\Games(19-05-2025).dat output\games_test.tlv
```

Specify all paths explicitly:

```bat
build\host\dat_to_tlv.exe assets_raw\Games(19-05-2025).dat output\games_test.tlv assets_raw\defs assets_raw\prefs\pack_types.ini
```

Disable logging:

```bat
build\host\dat_to_tlv.exe --no-log
```

Disable logging and specify paths:

```bat
build\host\dat_to_tlv.exe --no-log assets_raw\Games(19-05-2025).dat output\games_test.tlv assets_raw\defs assets_raw\prefs\pack_types.ini
```

## Expected Output

A successful run prints a summary similar to:

```text
DAT input:    assets_raw/Games(19-05-2025).dat
Output TLV:   output/Games(19-05-2025).tlv
CSV folder:   assets_raw/defs
Pack types:   assets_raw/prefs/pack_types.ini
DAT entries:  3861
Processed:    3861
Successful:   3861
Errors:       0
TLV entries:  11634
```

## Notes

- This started as a host-first standalone tool and now also builds as an Amiga target.
- It builds and runs entirely from `variant_backport_staging`.
- It does not require linking against source files outside this folder during the standalone build.
- The current implementation writes one aggregate TLV file for the whole DAT.
- The current implementation is focused on DAT-to-TLV conversion only. It does not yet perform profile-based filtering or variant scoring.
- The Makefile does not use PowerShell in normal build rules.
- On Amiga, use a manually increased stack such as `STACK 100000` before running the tool.
- Use `--no-log` to suppress logfile output when logging overhead is too slow.
