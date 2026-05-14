# tlv_demo — Facade Demonstration Program

`tlv_demo` is a minimal host-side command-line tool that exercises the public
`whdtlv` build facade end-to-end.  It converts a single Logiqx-style WHDLoad
DAT file into a binary TLV file ready for use on the Amiga, then prints a
one-line summary to `stdout`.

The program lives in `tools/tlv_demo/tlv_demo.c` and is built by the host
toolchain only (`make demo`).

---

## How It Works

1. **Parse** — The DAT XML file is read and each `<game>` entry is decoded.
2. **Tokenise** — Filenames are split into structured field tokens using the
   CSV definition tables in `assets_raw/defs/`.
3. **Validate** — Required fields are checked against the field registry.
4. **Emit** — Each record is serialised into the binary TLV format and written
   to the output file.
5. **Report** — On success the tool prints three counters:
   - `records_written` — entries successfully encoded.
   - `records_skipped` — entries discarded (missing required fields, etc.).
   - `groups_assigned` — variant groups identified across the written records.

Internally `tlv_demo` calls only `whdtlv_build_from_dat()` from the public
facade header `include/whdtlv/whdtlv.h`.  No internal pipeline headers are
used.

---

## Building

```bat
make demo
```

Output binary: `build/host/tlv_demo.exe` (Windows host).

---

## Usage

```
tlv_demo <dat_path> <defs_dir> <pack_types_path> <output_tlv_path>
```

| Argument | Description |
|---|---|
| `dat_path` | Path to the Logiqx-format `.dat` XML file. |
| `defs_dir` | Directory that contains the field CSV files (`assets_raw/defs/`). |
| `pack_types_path` | Path to `pack_types.ini` (`assets_raw/prefs/pack_types.ini`). |
| `output_tlv_path` | Destination `.tlv` file (created or overwritten). |

### Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success. |
| `1` | Wrong number of arguments — usage line is printed to `stderr`. |
| `2` | `whdtlv_build_from_dat` returned an error — error code is printed to `stderr`. |

---

## Examples

All examples assume the working directory is the repository root.

### Build a Games TLV

```bat
build\host\tlv_demo.exe ^
    "assets_raw\Dats\Game(2026-04-17).dat" ^
    assets_raw\defs ^
    assets_raw\prefs\pack_types.ini ^
    output\Game(2026-04-17).tlv
```

### Build a Demos TLV

```bat
build\host\tlv_demo.exe ^
    "assets_raw\Dats\Demo(2026-03-23).dat" ^
    assets_raw\defs ^
    assets_raw\prefs\pack_types.ini ^
    output\Demo(2026-03-23).tlv
```

### Build a Magazines TLV

```bat
build\host\tlv_demo.exe ^
    "assets_raw\Dats\Mags(2025-07-24).dat" ^
    assets_raw\defs ^
    assets_raw\prefs\pack_types.ini ^
    output\Mags(2025-07-24).tlv
```

### Example Output

```
records_written : 2847
records_skipped : 12
groups_assigned : 391
```

---

## Notes

- The `pack_type_id` is hard-coded to `0` (the first/default pack type) in
  `tlv_demo`.  To select a different pack type, embed the facade directly and
  pass the desired index to `whdtlv_build_from_dat()`.
- Logging is disabled by default.  Set `opts.enable_logging = 1` in the
  source if you need verbose pipeline output during development.
- The program intentionally uses no internal pipeline headers.  It is the
  canonical example of how an embedder (such as WHDFetch) should call the
  facade.
