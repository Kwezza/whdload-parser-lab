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

- Core engine folder: `src/core/`
- Harness folder: `src/harness/`
- CLI entrypoint (host): `src/harness/main.c`
- CLI entrypoint (Amiga): `src/harness/amiga_varianttest.c`
- CSV loading/lookup: `src/core/vh_csv.c`, `src/core/vh_csv.h`
- Filename parsing/token detection: `src/core/vh_parse.c`, `src/core/vh_parse.h`
- Grouping/select winner per group: `src/core/vh_group.c`, `src/core/vh_group.h`
- Profile loading + weights: `src/core/vh_profile.c`, `src/core/vh_profile.h`
- Scoring: `src/core/vh_score.c`, `src/core/vh_score.h`
- Field registry metadata: `src/core/vh_fields.c`, `src/core/vh_fields.h`
- String dedupe pool: `src/core/vh_string_pool.c`, `src/core/vh_string_pool.h`
- Allocation tracking: `src/core/vh_memtrack.c`, `src/core/vh_memtrack.h`

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
