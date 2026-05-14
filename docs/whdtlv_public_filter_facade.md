# whdtlv Public Filter Facade

## Purpose

`whdtlv_filter_to_list()` is the primary embedded API for running the TLV
filtering pipeline from a third-party program such as WHDFetch.

Given a TLV file, a defs directory, an optional profile, and an optional
search pattern, the function returns an allocated in-memory list of selected
archive filenames.  The caller iterates the list, consumes the names, then
frees it.  No internal headers are needed; no temporary files are written.

The separate `whdtlv_filter_to_file()` function is a convenience wrapper for
tools and harnesses that want a text-file output instead.

---

## Required Include

```c
#include "whdtlv/whdtlv.h"
```

That is the only include a caller needs.  Do not include internal headers such
as `whdtlv/filtering/tlv_filter.h` or `whdtlv/core/tlv_profile.h`.

---

## Build Requirements

Two flags must be present on the compiler command line:

```make
CFLAGS += -Iinclude   # for #include "whdtlv/whdtlv.h"
CFLAGS += -Isrc       # for internal cross-includes inside the subsystem
```

Both flags are required even when the caller only touches the public header.
The subsystem implementation files include each other using stable paths rooted
at `src/`, so `-Isrc` must be on the search path when compiling those files.

---

## Vendoring Into Another Program

Copy the following subtrees verbatim:

```text
whdfetch/
    include/
        whdtlv/
            whdtlv.h                  <- public header; only thing the caller includes
    src/
        whdtlv/
            whdtlv_filter_facade.c    <- public facade; include this in your build
            core/                     <- core pipeline (dat parser, field registry, ...)
            filtering/                <- internal filtering pipeline (not included by caller)
            io/                       <- write log, pack-types loader
            platform/                 <- platform_io, platform_string
            utils/                    <- crc32, prettify, logging shim
```

Add all `.c` files under `src/whdtlv/` to your build — including
`whdtlv_filter_facade.c` at the top of that tree and every `.c` file in
the `core/`, `filtering/`, `io/`, `platform/`, and `utils/` subdirectories.

The files in `tools_src/` are harness entry points and must **not** be
compiled into a library.

The caller only includes `include/whdtlv/whdtlv.h`.  The `filtering/`
subdirectory is an implementation detail; its headers are never included
directly by embedding code.

---

## Minimal Usage Example

```c
#include <string.h>
#include "whdtlv/whdtlv.h"

int run_filter(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList    results;
    unsigned int        i;
    int                 rc;

    whdtlv_filter_options_defaults(&opts);
    memset(&results, 0, sizeof(results));

    rc = whdtlv_filter_to_list(
        "PROGDIR:Games.tlv",
        "PROGDIR:defs",
        "PROGDIR:profiles/pal_aga_4mb.profile",
        "lotus*",
        &opts,
        &results,
        &summary
    );

    if (rc != WHDTLV_OK) {
        /* rc is a negative WHDTLV_FILTER_ERR_* code -- see Return Codes below */
        return rc;
    }

    for (i = 0; i < results.count; ++i) {
        /* Queue, download, or display results.items[i] */
    }

    whdtlv_string_list_free(&results);
    return WHDTLV_OK;
}
```

The same pattern works on both host and Amiga.  On Amiga, replace file paths
with `PROGDIR:`-relative paths as shown above, and ensure the stack is raised
before calling into the subsystem (`STACK 100000`).

---

## Ownership Rules

| Rule | Detail |
|---|---|
| The caller owns the list | `results` is allocated by `whdtlv_filter_to_list()` and must be freed by the caller. |
| Do not free individual strings | `results.items[i]` points into a single private block; freeing any one item is undefined behaviour. |
| Do not inspect `reserved` | `results.reserved` is the private allocation handle; the caller must treat it as opaque. |
| Always call `whdtlv_string_list_free()` | Even on an empty result (`count == 0`, `items == NULL`) the call is safe and completes in constant time. |
| Scope the list lifetime | On Amiga, free the list as soon as the caller has queued or consumed the names.  Do not hold it across long operations. |

---

## `search_pattern` Semantics

`search_pattern` is a **group-level** pre-filter.  It narrows which groups are
considered before profile scoring selects variants inside those groups.

| Pattern | Behaviour |
|---|---|
| `NULL` | No search filter; all groups are candidates. |
| `""` (empty string) | Same as NULL; no filter. |
| `"lotus"` | Case-insensitive substring match against the group name. |
| `"lotus*"` | Wildcard; `*` matches zero or more characters. |
| `"lotus?"` | Wildcard; `?` matches exactly one character. |

Profile scoring still selects the best variant or variants inside each matched
group.  The search does not bypass or replace profile selection.

---

## `profile_path` Semantics

`profile_path` controls which scoring profile is applied to variant selection.
It is optional; the pipeline runs without a profile file.

| Value | Behaviour |
|---|---|
| `NULL` | No profile loaded; built-in default scoring is used (one variant selected per group by internal rank). |
| `""` (empty string) | Same as `NULL`; no profile is loaded. |
| non-empty valid path | Loads the specified `.profile` file and applies it to variant scoring. |
| non-empty invalid path | Profile file cannot be opened or parsed; returns `WHDTLV_FILTER_ERR_PROFILE` (−6). |

The implementation test is `if (profile_path && profile_path[0] != '\0')`, so
both `NULL` and `""` are treated identically — they skip the profile-load step
and leave `has_profile = 0`.  When `has_profile == 0`, `tlv_select_run()` is
called with a `NULL` profile pointer and applies built-in default selection.

---

## `WhdTlvFilterOptions` Fields

| Field | Default | Description |
|---|---|---|
| `enable_logging` | `0` | `1` enables internal diagnostic logging (v1: accepted but has no effect). |
| `strict_crc` | `1` | `1` = abort if a CSV fingerprint mismatch is detected during CRC validation. |
| `reserved[6]` | `0` | Must be zero-initialised.  Call `whdtlv_filter_options_defaults()` to guarantee this. |

Always initialise with `whdtlv_filter_options_defaults()` before setting
individual fields.  This protects against future additions to the struct.

---

## `WhdTlvFilterSummary` Fields

| Field | Description |
|---|---|
| `variants_total` | Total variant records read from the TLV. |
| `groups_total` | Total groups present in the TLV. |
| `matched_groups` | Groups matched by `search_pattern`.  Equals `groups_total` when no search was applied; `0` when a search was applied but matched nothing. |
| `selected_variants` | Sum of per-group lane winners; equals `results.count` on success. |
| `selected_groups` | Groups that had at least one selection lane satisfied. |
| `rejected_variants` | Variants not chosen by any selection lane. |
| `rejected_groups` | Groups where no lane could be satisfied. |
| `selection_lanes` | Number of selection lanes used.  `1` for single-lane profiles; `>1` for multi-bucket profiles. |
| `crc_files_checked` | Total CSV files examined during CRC fingerprint validation. |
| `crc_mismatches` | Number of CSV files whose CRC did not match the TLV-embedded fingerprint. |
| `reserved[6]` | Reserved; always zero. |

Pass `NULL` for `summary` if the counters are not needed.

---

## Return Codes

`whdtlv_filter_to_list()` returns `WHDTLV_OK` (0) on success, including when
the result set is empty.  On failure it returns a **negative**
`WHDTLV_FILTER_ERR_*` code defined in `include/whdtlv/whdtlv.h`; `results`
is left in a safe empty state.  It is always safe to call
`whdtlv_string_list_free()` after any return, including on failure.

```c
WhdTlvStringList results;
memset(&results, 0, sizeof(results));

rc = whdtlv_filter_to_list(tlv_path, defs_dir, profile_path,
                            search_pattern, &opts, &results, &summary);

if (rc == WHDTLV_OK) {
    /* results may be empty (count == 0) -- this is not an error */
}

whdtlv_string_list_free(&results); /* always safe, even after failure */
```

| Code | Value | Meaning |
|---|---|---|
| `WHDTLV_FILTER_ERR_INVALID_ARG` | −1 | A required argument (`tlv_path`, `defs_dir`, or `results`) was NULL. |
| `WHDTLV_FILTER_ERR_IO` | −2 | TLV file could not be opened, or output file could not be written. |
| `WHDTLV_FILTER_ERR_PARSE` | −3 | TLV header or version field is invalid. |
| `WHDTLV_FILTER_ERR_EMPTY` | −4 | TLV contains no variants or no groups. |
| `WHDTLV_FILTER_ERR_CRC` | −5 | A CSV fingerprint is missing, unreadable, or does not match the TLV-embedded value. |
| `WHDTLV_FILTER_ERR_PROFILE` | −6 | Profile file could not be loaded or bound to the TLV field map. |
| `WHDTLV_FILTER_ERR_ALLOC` | −7 | Out of memory. |

All seven codes are negative; `WHDTLV_OK` is 0.  A caller can distinguish
success from failure with a single sign check (`rc < 0`).

The positive `WHDTLV_ERR_*` codes in the header (`WHDTLV_ERR_INVALID_ARG`,
`WHDTLV_ERR_IO`, `WHDTLV_ERR_PARSE`, `WHDTLV_ERR_ALLOC`) are reserved for
the builder API (`whdtlv_build_from_dat`) and are never returned by the
filtering facade.

---

## Optional File-Output Wrapper

`whdtlv_filter_to_file()` runs the same pipeline and writes one selected
filename per line to a text file:

```c
rc = whdtlv_filter_to_file(
    "PROGDIR:Games.tlv",
    "PROGDIR:defs",
    "PROGDIR:profiles/pal_aga_4mb.profile",
    "PROGDIR:selected.txt",   /* output file */
    NULL,                      /* no search pattern */
    &opts,
    &summary
);
```

This is implemented as a thin wrapper that calls `whdtlv_filter_to_list()`,
writes the list to the file, then frees the list.  Use it for harnesses and
regression tests; prefer `whdtlv_filter_to_list()` in embedded code.

---

## Amiga Integration Notes

- Include only `whdtlv/whdtlv.h`.  Do not include internal headers from
  `src/whdtlv/filtering/`.
- The reusable subsystem does not write to stdout or stderr.  All diagnostic
  output goes through the internal logging shim which is silent by default
  (`enable_logging = 0`).
- Raise the stack before calling into the subsystem: `STACK 100000`.
- Keep the list lifetime short.  Free it as soon as the caller has queued or
  consumed the names.
- The public facade has been validated on real Amiga hardware (see test
  record below).  Host and Amiga results are consistent.
- Compile the subsystem with vbcc in C89 mode.  All files under
  `src/whdtlv/filtering/` are C89-compatible.

---

## Amiga Validation Record

**Date:** 2026-05-14  
**Binary:** `build/amiga/test_filter_facade` (vbcc, 68000, C89)  
**TLV:** `output/Game(2026-04-17).tlv` (3 973 variants, 2 904 groups)  
**Defs:** `assets_raw/defs`  
**Profile:** `assets_raw/profiles/pal_aga_4mb.profile`  
**Stack:** `STACK 100000`

```
Results: 38 passed, 0 failed
```

| Test | Description | Result |
|---|---|---|
| 1 | Normal profile, no search — `matched_groups == groups_total` | PASS (8 checks) |
| 2 | Search match (`lotus*`) — matched_groups > 0, results > 0 | PASS (3 checks) |
| 3 | Search no-match — `matched_groups == 0`, `rc == WHDTLV_OK` | PASS (6 checks) |
| 4 | Multi-lane profile — `selection_lanes > 1` | PASS (3 checks) |
| 5 | Invalid profile path — `rc == WHDTLV_FILTER_ERR_PROFILE` (-6) | PASS (5 checks) |
| 6 | `whdtlv_string_list_free()` safety — NULL, zeroed, double-free | PASS (4 checks) |
| 7 | `whdtlv_filter_to_file()` wrapper — file line count matches | PASS (2 checks) |
| 8 | Empty search pattern (`""`) treated as no search | PASS (3 checks) |
| 9 | `profile_path == NULL` and `""` — same `has_profile = 0` branch, counts equal | PASS (5 checks) |

**Known Amiga behaviour note:** CSV files have Windows CRLF line endings.
`tlv_crc_validate.c` normalises `\r\n` → `\n` before hashing so the
validator produces the same CRC the Windows builder embedded.  Without
this normalisation every CSV fingerprint check would fail on Amiga.
