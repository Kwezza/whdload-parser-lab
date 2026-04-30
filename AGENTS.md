# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project Intent

This repo is an isolated lab for WHDLoad filename parsing, variant grouping, and profile-based selection logic intended for later integration into WHDFetch.

Read first:
- [whdload-parser-lab-project-documentation.md](whdload-parser-lab-project-documentation.md)
- [milestones.md](milestones.md)

## Canonical Commands

Run from repository root.

- Build host harness: `make`
- Run tests: `make test`
- Clean artifacts: `make clean`
- Build Amiga binary (vbcc): `make varianttest-amiga`

Notes:
- On Windows, `make test` runs PowerShell-based test scripts.
- `rg` may not be installed in this environment; prefer built-in workspace search tools when needed.

## Key Runtime Files

- CSV definitions: `data/Defs/*.csv`
- Profiles: `data/Profiles/*.profile`
- Example tests/data: `tests/`

## Source Map

- CLI entrypoint (host): `src/main.c`
- CLI entrypoint (Amiga): `src/amiga_varianttest.c`
- CSV loading/lookup: `src/vh_csv.c`, `src/vh_csv.h`
- Filename parsing/token detection: `src/vh_parse.c`, `src/vh_parse.h`
- Grouping/select winner per group: `src/vh_group.c`, `src/vh_group.h`
- Profile loading + weights: `src/vh_profile.c`, `src/vh_profile.h`
- Scoring: `src/vh_score.c`, `src/vh_score.h`
- Field registry metadata: `src/vh_fields.c`, `src/vh_fields.h`
- String dedupe pool: `src/vh_string_pool.c`, `src/vh_string_pool.h`
- Allocation tracking: `src/vh_memtrack.c`, `src/vh_memtrack.h`

## Conventions That Matter

- Keep public naming consistent with existing prefixes (`vh_` parser modules, `vt_` Amiga harness helpers).
- Keep C standard/tooling compatibility aligned with current Makefile (`-std=c99`, warning flags).
- Preserve conservative grouping behavior in parser changes unless explicitly requested.
- Preserve deterministic selection order (tie-break by original input index).
- Keep profile behavior stable: include/exclude semantics and weight handling.

## Pitfalls

- Be careful with path assumptions: host tests use relative `data/` and `tests/` paths from repo root.
- Buffer safety matters: many operations rely on fixed-size buffers and checked `snprintf` behavior.
- Avoid introducing dependencies or language features that break vbcc/Amiga build compatibility.

## Change Validation

For parser/selector behavior changes, run at minimum:
- `make`
- `make test`

If output format changes, update expected artifacts under `tests/expected/` and corresponding test assertions together.
