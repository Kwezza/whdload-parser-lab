# WHDLoad Parser Lab

## Purpose

**WHDLoad Parser Lab** is a small, standalone development testbed for experimenting with WHDLoad archive list parsing, variant detection, and selection logic outside the main WHDFetch codebase.

It exists so that the parsing and variant-selection work can be developed, tested, profiled, and refined independently before being folded back into WHDFetch. The aim is to keep the main WHDFetch project stable while providing a dedicated space to investigate the more complex logic needed to identify related WHDLoad packs and choose the most appropriate version for a particular user configuration.

## Why This Repository Exists

WHDFetch currently has a focused command-line role: download WHDLoad packs from known archive lists and extract them on the Amiga. The next development phase introduces more sophisticated behaviour, especially around avoiding duplicate or unwanted variants of the same game or demo.

For example, a user might want to prefer:

- French versions where available.
- AGA versions where they are useful and supported.
- Higher-memory versions where the user has enough RAM.
- Standard ECS/OCS versions where no better match exists.
- English versions only when no preferred language variant is available.

This requires parsing archive filenames, identifying groups of related variants, assigning weights, and selecting a preferred candidate. That logic is deliberately more experimental than the existing WHDFetch download workflow, so it benefits from being developed separately.

The testbed allows that work to happen without risking regressions in the released WHDFetch tool.

## Background

The idea comes from an earlier, shelved GUI-based version of the WHDLoad tooling. That version contained more advanced lookup and prettifying systems, but it became too complex for the initial WHDFetch release.

WHDFetch was released in a simpler, more maintainable form first. WHDLoad Parser Lab now provides a way to backport the useful parts of the shelved system in smaller, testable stages.

The goal is not to recreate the full GUI tool. The goal is to extract and validate the practical parsing and variant-selection behaviour that can be useful in WHDFetch while keeping WHDFetch itself command-line based and backwards compatible.

## Main Goals

WHDLoad Parser Lab aims to:

1. Parse WHDLoad DAT/listing files generated from archive sources.
2. Extract meaningful metadata from WHDLoad archive filenames.
3. Detect related variants of the same title.
4. Identify language tags, hardware tags, memory tags, AGA/ECS/OCS variants, beta markers, alternate versions, and similar filename indicators.
5. Experiment with weighted selection rules.
6. Produce clear diagnostic output so decisions can be reviewed by a human or by an AI coding agent.
7. Build a small Amiga-compatible command-line harness using vbcc.
8. Test behaviour against real archive listing data before integration into WHDFetch.

## Non-Goals

This repository is not intended to become a complete WHDLoad manager.

It should not duplicate the whole WHDFetch application and should avoid expanding into unrelated areas such as:

- Download management.
- Extraction management.
- GUI design.
- Full title prettification.
- Full metadata database generation.
- Replacement of WHDFetch.

The repository should remain a focused laboratory for parser and selector behaviour.

## Relationship to WHDFetch

WHDFetch remains the released end-user tool.

WHDLoad Parser Lab is a supporting development project used to prove and refine logic that may later be merged into WHDFetch. Any code or algorithm promoted back to WHDFetch should be:

- Amiga-friendly.
- Memory-conscious.
- Compatible with the intended C standard and compiler choices.
- Suitable for command-line operation.
- Backwards compatible with existing WHDFetch usage.
- Simple enough to maintain without pulling in the complexity of the earlier shelved GUI version.

## Current Test Input

The initial practical test target is the WHDLoad Games DAT/listing file, for example:

```text
Bin/Amiga/temp/Dat files/Games(2026-04-17).txt
```

The test harness should read this file, process the archive entries, and output results both to:

- the terminal, for immediate review on the Amiga or emulator; and
- a text file, for later inspection by the developer or an AI coding agent.

## Expected Output

The output should be designed for debugging and review rather than polished end-user presentation.

Useful output may include:

- Parsed title/group key.
- Original archive filename.
- Detected tags.
- Language detection result.
- Hardware/platform detection result.
- Memory requirement detection result.
- Variant grouping result.
- Weighting score.
- Selected preferred variant.
- Rejected variants and reasons.
- Unknown or ambiguous tags requiring review.

The output should make it easy to understand why one archive was preferred over another.

## Design Principles

### Keep It Small

The testbed should remain intentionally narrow. Its value is that it can be modified quickly without the overhead of the full WHDFetch project.

### Use Real Data

The parser should be tested against real WHDLoad archive listings rather than only artificial examples. Real filenames contain inconsistencies, edge cases, and historical naming patterns that are easy to miss in hand-written tests.

### Prefer Transparent Decisions

Variant selection should not be a black box. If a file is selected or rejected, the output should explain the decision clearly enough that the rules can be corrected.

### Stay Amiga-Compatible

The final logic is intended to run on Amiga systems, so the testbed should avoid designs that assume modern PC resources. Memory use, allocation strategy, string handling, and compiler compatibility should all be considered early.

### Avoid Over-Engineering

The shelved GUI version had powerful lookup and presentation systems, but WHDFetch does not need all of that complexity. This project should extract the useful parsing behaviour without inheriting unnecessary architecture.

## Memory and Performance Considerations

Although the source DAT files may be relatively small on disk, naive parsing can inflate memory usage significantly if each entry stores multiple duplicated strings, expanded metadata fields, lookup tables, temporary buffers, and grouping structures.

Part of this testbed's role is to make that cost visible and to explore leaner approaches, such as:

- parsing line-by-line where possible;
- avoiding unnecessary string duplication;
- using fixed-size buffers where appropriate;
- storing offsets or compact references rather than full copies;
- separating diagnostic builds from release-style builds;
- measuring memory use on realistic data.

## Possible Future Integration Path

A sensible integration path back into WHDFetch would be:

1. Build the parser harness in this repository.
2. Validate filename parsing against the full Games listing.
3. Add diagnostic reports for unknown tags and grouping problems.
4. Implement a basic weighting system.
5. Test selection profiles, for example language preference, AGA preference, memory preference.
6. Reduce memory use and simplify the data structures.
7. Move stable parser/selector modules into WHDFetch.
8. Add WHDFetch command-line options without breaking existing behaviour.
9. Keep the testbed available for future experiments and regression tests.

## Example Use Cases

### Language Preference

A French user may want French or multilingual packs preferred over English-only packs, but still wants an English version where no French version exists.

### Hardware Preference

A user with an AGA-capable Amiga may prefer AGA versions, while an ECS/OCS user may want those variants ignored or deprioritised.

### Memory Preference

A user with 8 MB of Fast RAM may prefer enhanced or higher-memory versions where they provide a better experience, while a lower-memory setup may need safer defaults.

### Duplicate Reduction

Instead of downloading multiple variants of the same game, WHDFetch could eventually download the best-matching version for the user's configured profile.

## Repository Scope

This repository should contain:

- Parser source files.
- Variant-selection logic.
- Test harness code.
- Example input path assumptions.
- Build files for the Amiga target using vbcc.
- Diagnostic output examples.
- Notes on ambiguous filename patterns.

It may also contain PC-side helper tools if useful, but the core logic should remain suitable for Amiga use.

## Success Criteria

The project is successful if it produces a small, understandable parser and selector that can:

- process real WHDLoad archive listings;
- group related variants accurately enough to be useful;
- select preferred variants according to configurable weighting rules;
- produce reviewable diagnostic output;
- compile for Amiga using the intended toolchain;
- run with acceptable memory use;
- provide code that can be safely merged back into WHDFetch.

## Summary

WHDLoad Parser Lab is a safe experimental space for developing the next layer of WHDFetch intelligence. It keeps the risky, exploratory parser work separate from the released tool, while still aiming to produce practical, Amiga-compatible code that can eventually improve WHDFetch's handling of WHDLoad variants.
