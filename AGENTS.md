# dat_to_tlv — Agent Instructions

This repository is a standalone staging area for converting Logiqx-style WHDLoad DAT files into a binary TLV format for Amiga use.

Read these first when you need deeper context:

- [README.md](README.md) for the current folder map and pipeline overview.
- [.github/instructions/dat-to-tlv-codebase.instructions.md](.github/instructions/dat-to-tlv-codebase.instructions.md) for detailed codebase conventions.
- [docs/HANDOVER_2026-05-01.md](docs/HANDOVER_2026-05-01.md) for the known Amiga runtime crash and recent handover notes.
- [notes/backport_inventory.md](notes/backport_inventory.md) for what is active now versus staged for later milestones.

## Build And Run

The Makefile uses `cmd` as its shell. Do not add or rely on PowerShell in normal make rules.

```bat
make
make TARGET=host
make TARGET=amiga
make PROFILE=1
make run
make clean
make help
```

Build outputs:

- `build/host/dat_to_tlv.exe`
- `build/amiga/dat_to_tlv`

## What Is Active

The current built pipeline is:

```text
DAT parse -> filename tokenize -> CSV validation -> field registry -> TLV output
```

Active ownership split:

- `app_src/`: standalone harness and minimal DAT parsing.
- `src_raw/`: active TLV pipeline logic.
- `src/`: stable platform, logging, and utility support.
- `include_raw/` and `include/`: headers for those two layers.

Do not describe the staged `filter_*`, `variant_*`, `active_set.c`, or `profile_loader.c` modules as current runtime behavior unless you also wire them into the `Makefile` build.

## Code Rules

- Use only the existing platform guards:

```c
#ifdef PLATFORM_AMIGA
    /* Amiga-specific code */
#else
    /* Host code */
#endif
```

- Keep `src_raw/` compatible with vbcc C89 mode. Avoid C99-only syntax there.
- If you add a new pipeline module, add the `.c` file in `src_raw/`, its header in `include_raw/`, and update the `SRC` list in `Makefile`.
- Keep docs aligned with the current runtime defaults in `app_src/main.c` and do not reintroduce stale staging-path references.

## Known Pitfalls

- Host builds work; the Amiga binary is still known to crash at runtime.
- On Amiga, raise the stack before running: `STACK 100000`.
- `VBCC` defaults to `C:/VBCC`; override it if your vbcc install lives elsewhere.
