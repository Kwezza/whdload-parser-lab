# Copilot Agent Prompt: Add Public Filtering Facade Returning an In-Memory Filename List

## Context

This repository contains the standalone `variant_backport_staging` DAT-to-TLV and TLV filtering pipeline used for Amiga/host builds.

The source tree has recently been cleaned up so the reusable subsystem is intended to live under a namespaced source area, for example:

```text
src/whdtlv/
    core/
    filtering/
    io/
    platform/
    utils/

include/whdtlv/
    whdtlv.h

tools_src/
    dat_to_tlv_main.c
    filter_harness_main.c
    filter_demo_main.c
```

The current public facade header, `include/whdtlv/whdtlv.h`, exposes the TLV builder side only. It lets a caller create a TLV from a DAT file, but it does not expose the runtime filtering/search side in a clean way.

The goal of this task is to add a small public filtering facade so a caller such as WHDFetch can do this without including internal headers or calling internal runtime functions directly:

```text
TLV file + defs directory + profile file + optional search pattern
    -> in-memory list of selected archive filenames
```

This avoids the current harness-style weakness where selected filenames are written to a text file and the caller would then have to reopen and parse that file.

The reusable subsystem should remain embeddable in another program. In particular, WHDFetch should be able to vendor/copy the `whdtlv` subsystem into its own `src` tree and include only the public facade header.

## Important Design Constraints

1. Keep the public API small.
2. Do not expose internal structs such as variant views, group sets, bound profiles, selection plans, allow lists, or scanner internals.
3. The caller should only need to include:

```c
#include "whdtlv/whdtlv.h"
```

4. The primary runtime filtering API should return an allocated in-memory list of filenames.
5. A text-file output function may remain as a convenience wrapper for tools/harnesses, but it should not be the primary embedded API.
6. The code must remain C89-friendly and Amiga-aware.
7. Avoid large numbers of small allocations where practical.
8. Avoid fragile relative includes such as `../myheader.h`. Prefer stable include paths controlled by the Makefile, for example:

```c
#include "whdtlv/filtering/filter_runtime.h"
#include "whdtlv/core/variant_index.h"
```

9. Any data written into TLV files must preserve the project's existing endian rules. This task should not alter the TLV wire format.
10. The reusable filtering subsystem should not write directly to stdout or stderr. The harness/tool layer may print summaries.

## Desired Public API

Extend `include/whdtlv/whdtlv.h` with a filtering facade similar to the following. Adjust names only if there is a strong reason, and keep the public shape simple.

```c
/*------------------------------------------------------------------------*/
/* Filter options */

typedef struct WhdTlvFilterOptions {
    int enable_logging;       /* 0 = off, 1 = on */
    int strict_crc;           /* 1 = abort on CSV fingerprint mismatch */
    int reserved[6];          /* must be zero-initialised */
} WhdTlvFilterOptions;

/*------------------------------------------------------------------------*/
/* Filter summary */

typedef struct WhdTlvFilterSummary {
    unsigned int variants_total;
    unsigned int groups_total;
    unsigned int matched_groups;
    unsigned int selected_variants;
    unsigned int selected_groups;
    unsigned int rejected_variants;
    unsigned int rejected_groups;
    unsigned int selection_lanes;
    unsigned int crc_files_checked;
    unsigned int crc_mismatches;
    unsigned int reserved[6];
} WhdTlvFilterSummary;

/*------------------------------------------------------------------------*/
/* Returned filename list */

typedef struct WhdTlvStringList {
    unsigned int count;
    char       **items;
    void        *reserved;    /* internal/private; caller must not inspect */
} WhdTlvStringList;

void whdtlv_filter_options_defaults(WhdTlvFilterOptions *opts);

int whdtlv_filter_to_list(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvStringList           *results,
    WhdTlvFilterSummary        *summary
);

void whdtlv_string_list_free(WhdTlvStringList *list);
```

### Optional Convenience API

If straightforward, also add this as a wrapper around `whdtlv_filter_to_list()`:

```c
int whdtlv_filter_to_file(
    const char                 *tlv_path,
    const char                 *defs_dir,
    const char                 *profile_path,
    const char                 *output_list_path,
    const char                 *search_pattern,
    const WhdTlvFilterOptions  *options,
    WhdTlvFilterSummary        *summary
);
```

This is useful for the existing harness and regression tests, but the in-memory list function should be treated as the main embedded API.

## Behaviour Requirements

### `whdtlv_filter_options_defaults()`

Should safely initialise the options struct.

Expected defaults:

```text
enable_logging = 0
strict_crc     = 1
reserved[]     = 0
```

If `options == NULL` is passed to `whdtlv_filter_to_list()`, the implementation should behave as if default options were supplied.

### `whdtlv_filter_to_list()`

The function should:

1. Validate required arguments.
2. Initialise `results` to a safe empty state before doing work.
3. Initialise `summary` to zero if supplied.
4. Load and validate the TLV file.
5. Validate CSV fingerprints using `defs_dir`.
6. Load and bind the requested `.profile`.
7. Apply the optional group-level `search_pattern` if non-NULL and non-empty.
8. Score and select variants using the existing filtering runtime.
9. Return selected archive filenames in `results`.
10. Fill `summary` with the same key counters currently printed by the harness.
11. Return a project-standard success/error code.
12. On failure, free any partial allocations and leave `results` in a safe empty state.

The caller should own the returned list and must release it by calling:

```c
whdtlv_string_list_free(&results);
```

### Empty result behaviour

An empty result set is valid and should not be treated as an error. For example, a search pattern that matches no groups should return success with:

```text
results.count = 0
results.items = NULL
```

or an equivalent safe empty representation.

### Search behaviour

The optional `search_pattern` is a group-level pre-filter, not a variant-level filter.

Examples:

```text
NULL       -> no search filter
""         -> no search filter
"lotus"    -> case-insensitive substring match
"lotus*"   -> wildcard match
"lotus?"   -> wildcard match where ? is one character
```

The existing runtime behaviour should be preserved:

```text
Search narrows candidate groups.
Profile scoring still selects the best variant or variants inside each matched group.
```

### Memory management

The public list should be easy for callers to consume:

```c
for (i = 0; i < results.count; ++i) {
    printf("%s\n", results.items[i]);
}
```

But internally, avoid 5,000 separate string allocations if practical.

Preferred implementation:

```text
one allocation for the pointer table
one allocation for the packed string data
```

or a single private allocation that owns both.

The public struct contains `void *reserved` so the implementation can hide its private allocation details. The caller must not inspect or free `reserved` directly.

`whdtlv_string_list_free()` must:

1. Accept NULL safely.
2. Free all memory owned by the list.
3. Reset the struct to a safe empty state.

### Expected memory scale

The foreseeable maximum result set is around 5,000 filenames from one TLV filter. This is acceptable as a temporary list, but keep allocations compact and Amiga-friendly.

Rough scale:

```text
5,000 filenames × roughly 80 bytes including NUL = around 400 KB
5,000 pointers × 4 bytes on Amiga                = around 20 KB
```

Avoid unnecessary extra copies beyond what is required to return a stable owned list.

## Implementation Plan Requested

Create an implementation plan before editing code. The plan should identify:

1. Which existing internal filtering functions already produce the selected names.
2. Which function currently writes the text output file.
3. The cleanest place to add the public facade implementation, for example:

```text
src/whdtlv/integration/whdtlv_filter_facade.c
```

or another appropriate existing integration/facade file.

4. Whether the existing filter pipeline already has a reusable result vector/list internally.
5. Whether a small adapter layer is needed to collect selected names into `WhdTlvStringList`.
6. How summary counters will be mapped from the existing runtime/harness data into `WhdTlvFilterSummary`.
7. How failure cleanup will be handled.
8. What host and Amiga builds must be updated.
9. What tests or harness changes are required.

Do not expose internals merely to make this easy. Add a small adapter layer if necessary.

## Testing Requirements

Add or update tests/harness coverage for the public facade.

At minimum, test on the host build first:

### Test 1: Normal profile, no search

Input:

```text
TLV:     existing generated Games TLV
Profile: assets_raw/profiles/pal_aga_4mb.profile
Search:  none
```

Expected:

```text
whdtlv_filter_to_list() returns success
results.count matches selected_variants in summary
results.count is greater than zero
all returned strings are non-NULL and non-empty
summary.groups_total is greater than zero
summary.selection_lanes is 1, or the existing single-lane equivalent
```

### Test 2: Search match

Input:

```text
Search: lotus*
```

Expected:

```text
success
matched_groups is greater than zero
results.count is greater than zero
returned filenames belong to matched Lotus groups
```

### Test 3: Search no-match

Input:

```text
Search: thisshouldnotmatchanything*
```

Expected:

```text
success
results.count == 0
matched_groups == 0
no crash
no leaked allocations
```

### Test 4: Multi-lane profile

Input:

```text
Profile: assets_raw/profiles/multi_bucket_reference.profile
```

Expected:

```text
success
summary.selection_lanes > 1
results.count == summary.selected_variants
multiple archive filenames may be returned for some game groups
```

### Test 5: Invalid profile path

Expected:

```text
failure return code
results left empty
summary is either zeroed or contains only safe partial data
no leaked allocations
```

### Test 6: `whdtlv_string_list_free()` safety

Verify:

```text
freeing an empty list is safe
freeing a populated list is safe
calling free twice is safe if the caller passes the same struct again
```

### Test 7: Optional file wrapper

If `whdtlv_filter_to_file()` is implemented:

```text
use whdtlv_filter_to_list() internally
write one filename per line
verify line count equals summary.selected_variants
```

## Implementation Guide To Produce

In addition to code changes, create a short implementation guide for future coders or AI agents. Suggested location:

```text
docs/whdtlv_public_filter_facade.md
```

The guide should explain how to incorporate the public API into another program such as WHDFetch.

The guide should include:

1. The purpose of the public facade.
2. Required include line:

```c
#include "whdtlv/whdtlv.h"
```

3. Required build/include path expectation, for example:

```make
CFLAGS += -Isrc
CFLAGS += -Iinclude
```

4. Example source tree when vendored into another program:

```text
whdfetch/
    include/
        whdtlv/
            whdtlv.h
    src/
        whdfetch_main.c
        whdtlv/
            core/
            filtering/
            io/
            platform/
            utils/
```

5. Minimal example code using `whdtlv_filter_to_list()`:

```c
#include "whdtlv/whdtlv.h"

int run_filter(void)
{
    WhdTlvFilterOptions opts;
    WhdTlvFilterSummary summary;
    WhdTlvStringList results;
    unsigned int i;
    int rc;

    whdtlv_filter_options_defaults(&opts);

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
        return rc;
    }

    for (i = 0; i < results.count; ++i) {
        /* Queue, download, or display results.items[i]. */
    }

    whdtlv_string_list_free(&results);
    return WHDTLV_OK;
}
```

6. Explanation of ownership rules:

```text
The caller owns the returned list.
The caller must not free individual strings.
The caller must call whdtlv_string_list_free().
The caller must not inspect the reserved field.
```

7. Explanation of `search_pattern`:

```text
NULL or empty means no search.
Plain text means case-insensitive substring match.
* and ? provide wildcard matching.
Search filters groups, not individual variants.
```

8. Explanation of `summary` fields.
9. Example use of the optional file-output wrapper if implemented.
10. Notes for Amiga integration:

```text
Use the facade rather than internal headers.
Do not rely on stdout/stderr from the reusable subsystem.
Keep the result list lifetime short.
Free it as soon as the caller has queued or consumed the names.
```

## Acceptance Criteria

The task is complete when:

1. `include/whdtlv/whdtlv.h` exposes the filtering list API.
2. `whdtlv_filter_to_list()` works from a clean host build.
3. `whdtlv_string_list_free()` is implemented and safe.
4. The existing builder API still works.
5. Existing harness behaviour is preserved or updated cleanly.
6. The filter harness can be updated to use the public facade instead of internal calls where sensible.
7. No internal filtering structs are exposed in the public header.
8. The public facade compiles under the project's C89/Amiga constraints.
9. The implementation guide exists and shows how another program can use the API.
10. Tests or harness checks cover normal, search, no-match, multi-lane and failure cases.

## Final Notes

This task is not intended to redesign the filtering engine. It is a facade/API cleanup task.

Prefer small, conservative changes:

```text
existing internal pipeline -> small adapter -> public WhdTlvStringList
```

Do not change the TLV format, scoring rules, slash-bucket behaviour, group_id handling, archive_info handling, or CSV fingerprint policy unless an existing bug is found and documented.
