# Visual Studio Copilot Agent Prompt: WHDTLV Source Tree Cleanup

## Context

This repository is the standalone `variant_backport_staging` DAT-to-TLV pipeline for Amiga and host builds. It processes RetroPlay WHDLoad DAT entries, parses metadata encoded in filenames, writes compact TLV data, and runs a runtime filtering system that can select the best or next-best variant using weighted profile preferences.

The current folder structure has grown organically and is now confusing before release. There are currently multiple overlapping source/header areas such as:

```text
include/
include_raw/
src/
src_raw/
app_src/
```

The intent of this cleanup is to make the folder layout self-explanatory, keep reusable WHDTLV code boxed off from test/tool entry points, and make the code easier to vendor later inside another project such as WHDFetch.

The desired architecture is:

```text
tools_src/       = command-line tools, demos, harnesses and executable entry points
src/whdtlv/      = reusable WHDTLV implementation code
include/whdtlv/  = public WHDTLV facade header only
assets_raw/      = editable input assets/configuration, not source code
output/          = generated outputs
build/           = host/amiga build products
```

The reusable subsystem should be movable into another project as a single `whdtlv` folder without requiring source files to use fragile relative includes such as `../myheader.h`.

## High-Level Goal

Refactor the repository layout so that:

1. There is one primary implementation source tree under `src/whdtlv/`.
2. Public headers live only under `include/whdtlv/`.
3. Private headers live beside their matching `.c` files under `src/whdtlv/...`.
4. Tool/demo/harness entry points live under `tools_src/`.
5. Existing `src_raw`, `include_raw`, and vague duplicate include/source layout is removed or emptied once migrated.
6. The project still builds and runs on the host first.
7. The code remains Amiga-oriented and portable, avoiding non-C89 assumptions unless the project already explicitly allows them.

## Important Constraints

- Preserve current behaviour.
- Do not rewrite logic unless necessary to make the move compile.
- Do not mix WHDTLV reusable code directly into generic top-level `src/io`, `src/platform`, or `src/utils` folders. Keep it namespaced under `src/whdtlv/`.
- Avoid source includes like `../foo.h` or `../../bar.h`.
- Prefer stable include paths such as:

```c
#include "whdtlv/core/tlv_builder.h"
#include "whdtlv/filtering/filter_runtime.h"
#include "whdtlv/platform/platform_io.h"
```

- The build system should provide include paths, for example:

```make
-Iinclude
-Isrc
```

- Keep `assets_raw/` as-is. It contains DAT input files, CSV definitions, profiles and prefs such as:

```text
assets_raw/defs/
assets_raw/prefs/pack_types.ini
assets_raw/profiles/
assets_raw/*.dat
```

- Keep `output/` for generated files.
- Keep `build/host` and `build/amiga` for build products if already used.
- Do not bury command-line tool entry points inside the reusable library.

## Target Folder Structure

Create or migrate toward this structure:

```text
assets_raw/
    defs/
    prefs/
    profiles/
    *.dat

build/
    amiga/
    host/

docs/

include/
    whdtlv/
        whdtlv.h

src/
    whdtlv/
        core/
            active_set.c/.h
            csv_cache.c/.h
            error_handling.c/.h
            field_registry.c/.h
            filename_processor.c/.h
            group_util.c/.h
            slug_util.c/.h
            tlv_builder.c/.h
            tlv_profile.c/.h
            variant_index.c/.h
            variant_iterator.c/.h

        filtering/
            filter_pipeline.c/.h
            filter_profile.c/.h
            filter_runtime.c/.h
            profile_loader.c/.h

        io/
            pack_types_loader.c/.h
            writelog.c/.h

        platform/
            platform_io.c/.h
            platform_string.c/.h

        utils/
            crc32.c/.h
            prettify.c/.h

tools_src/
    dat_to_tlv_main.c
    filter_demo_main.c
    filter_harness_main.c

output/
notes/
```

This is a target, not a rigid list. If current filenames differ, preserve their names unless there is a clearly safe reason to rename them.

## File Classification Guidance

Use this as the first-pass classification.

### Move to `src/whdtlv/core/`

Likely core TLV construction, metadata and variant model internals:

```text
active_set.c
csv_cache.c
error_handling.c
field_registry.c
filename_processor.c
group_util.c
slug_util.c
tlv_builder.c
tlv_profile.c
variant_index.c
variant_iterator.c
```

Move their matching private headers beside them if they exist.

### Move to `src/whdtlv/filtering/`

Filtering, profile binding, runtime scoring and selection:

```text
filter_pipeline.c
filter_profile.c
filter_runtime.c
profile_loader.c
```

Move their matching private headers beside them if they exist.

### Move to `src/whdtlv/io/`

File/config/logging helpers used by the tools or subsystem:

```text
pack_types_loader.c
writelog.c
```

Move matching headers beside them.

### Move to `src/whdtlv/platform/`

Host/Amiga portability layer:

```text
platform_io.c
platform_string.c
platform.h, if it is not intended as a public API
```

Move matching headers beside them.

### Move to `src/whdtlv/utils/`

Small reusable helper code:

```text
crc32.c
prettify.c
```

If `prettify.c` is only used for test/harness output and not the reusable library, consider placing it under `tools_src/support/` instead. Prefer the least disruptive option that compiles cleanly.

### Move executable entry points to `tools_src/`

Any file containing `main()` for the standalone builder, filter harness, regression runner, demo, or command-line proof-of-concept should be moved to `tools_src/`.

Examples:

```text
dat_to_tlv_main.c
filter_harness_main.c
filter_demo_main.c
```

If current `main()` files have different names, keep their existing names unless a rename is simple and updates cleanly.

## Public Header Policy

The intended public interface should be a small facade:

```text
include/whdtlv/whdtlv.h
```

External callers should include only:

```c
#include "whdtlv/whdtlv.h"
```

If the current public facade is named:

```text
include/integration/whdtlv_integration.h
```

then either:

1. Move/rename it to `include/whdtlv/whdtlv.h`, or
2. Create `include/whdtlv/whdtlv.h` as a thin wrapper around the existing facade during transition.

Prefer option 1 if the update is straightforward. Prefer option 2 if renaming causes too much churn.

Do not expose every private subsystem header in `include/`. Most headers should live beside the `.c` files under `src/whdtlv/...`.

## Include Path Policy

After migration, update includes to use stable namespaced paths.

Good examples:

```c
#include "whdtlv/core/tlv_builder.h"
#include "whdtlv/filtering/filter_runtime.h"
#include "whdtlv/io/pack_types_loader.h"
#include "whdtlv/platform/platform_io.h"
#include "whdtlv/utils/crc32.h"
```

Avoid:

```c
#include "../tlv_builder.h"
#include "../../include_raw/filtering/profile_binder.h"
#include "../../../platform.h"
```

Build files should add the include roots instead:

```make
CFLAGS += -Iinclude
CFLAGS += -Isrc
```

If the Amiga build system uses a different variable, update the equivalent variable there too.

## Build System Tasks

Update all relevant build scripts, Makefiles or project files so that:

1. Source file paths point to the new `src/whdtlv/...` and `tools_src/...` locations.
2. Include paths contain `include` and `src`.
3. Old `src_raw`, `include_raw`, and old top-level `src/io`, `src/platform`, `src/utils` references are removed once replaced.
4. Host build still works.
5. Amiga build file paths are updated, but avoid Amiga-specific testing if not available locally.

Be careful with Windows host builds. Avoid introducing shell commands that assume Unix-only tools if the existing build is designed to work under Windows.

## Suggested Step-by-Step Plan

### Phase 1: Inventory

1. Find every `.c` and `.h` file under:

```text
src/
src_raw/
include/
include_raw/
app_src/
```

2. Identify files containing `main()`.
3. Identify all private headers and their matching `.c` files.
4. Identify the current public facade header.
5. Record current build entry points and expected output binaries.

Create a short temporary note in the agent response or working notes describing the planned moves before changing files.

### Phase 2: Create New Folders

Create:

```text
src/whdtlv/core/
src/whdtlv/filtering/
src/whdtlv/io/
src/whdtlv/platform/
src/whdtlv/utils/
include/whdtlv/
tools_src/
```

Only create `tools_src/support/` if needed.

### Phase 3: Move Files

Move implementation files according to the classification guidance above.

Move private headers beside their `.c` files.

Move executable entry points into `tools_src/`.

Do not delete old empty folders until the build is fixed.

### Phase 4: Update Includes

Update source includes to use the new stable namespaced paths.

Examples:

```c
#include "whdtlv/core/field_registry.h"
#include "whdtlv/filtering/profile_loader.h"
#include "whdtlv/platform/platform_string.h"
```

For same-folder private includes, either of these is acceptable:

```c
#include "tlv_builder.h"
```

or:

```c
#include "whdtlv/core/tlv_builder.h"
```

Prefer namespaced includes where it makes future vendoring into WHDFetch cleaner and avoids ambiguity.

### Phase 5: Public Facade

Create or move the public facade to:

```text
include/whdtlv/whdtlv.h
```

This header should expose only the small API needed by external code. It should not force external callers to include private builder/filter internals.

If there is already a working facade under `include/integration/whdtlv_integration.h`, either move its contents or provide a compatibility wrapper. If a compatibility wrapper is used, add a TODO comment explaining that it can be removed after callers migrate.

### Phase 6: Update Build Scripts

Update Makefiles/project files so all builds refer to the new locations.

Expected include roots:

```make
-Iinclude
-Isrc
```

Expected source roots:

```text
src/whdtlv/...
tools_src/...
```

Remove references to:

```text
src_raw/
include_raw/
app_src/
```

unless a transitional compatibility file remains temporarily.

### Phase 7: Build and Test on Host

Run the existing host build and the existing host tests/harnesses.

Suggested checks:

1. Clean build from scratch.
2. Build `dat_to_tlv` or the current equivalent host tool.
3. Run a small TLV build using existing `assets_raw` inputs.
4. Run the filtering harness against an existing TLV/profile if available.
5. Confirm generated output matches previous behaviour where practical.

Do not change algorithmic behaviour to make tests pass unless there is a genuine bug caused by the migration.

### Phase 8: Cleanup

Once host build and tests pass:

1. Remove empty obsolete folders if safe:

```text
src_raw/
include_raw/
app_src/
```

2. Remove obsolete include path entries.
3. Search for stale references to old paths.
4. Search for fragile `../` include paths and replace where sensible.
5. Leave `assets_raw/`, `output/`, `docs/`, `notes/`, and `build/` intact.

### Phase 9: Documentation

Add or update a short source-layout note, preferably:

```text
docs/source_layout.md
```

or a short section in an existing project overview.

Include this explanation:

```text
assets_raw/      Editable source assets: DAT files, CSV definitions, profiles and prefs.
tools_src/       Standalone command-line tools, demos and harness entry points.
src/whdtlv/      Reusable WHDTLV implementation code.
include/whdtlv/  Public facade used by external callers.
output/          Generated TLV files and result files.
build/           Host and Amiga build products.
```

Also mention that `src/whdtlv/` is intentionally namespaced so it can be copied into WHDFetch without colliding with WHDFetch's own source folders.

## Validation Checklist

Before finishing, verify:

- [ ] No `.c` or private `.h` files remain in `include_raw/`.
- [ ] No reusable implementation files remain in `src_raw/`.
- [ ] Public API is available from `include/whdtlv/whdtlv.h`.
- [ ] Tool/harness `main()` files are under `tools_src/`.
- [ ] Build scripts use `-Iinclude` and `-Isrc` or equivalent.
- [ ] No new fragile `../` include paths were introduced.
- [ ] Host build succeeds.
- [ ] Existing host tests or harnesses run successfully.
- [ ] Search for `src_raw`, `include_raw`, and `app_src` returns no build-critical references.
- [ ] Source layout documentation exists.

## If Something Is Ambiguous

Make the smallest safe change that improves structure without changing behaviour.

If a file is hard to classify, keep it under the closest WHDTLV namespace folder and document why in the final summary.

If a rename causes excessive churn, prefer moving the file without renaming it.

If the public facade rename is risky, create a wrapper `include/whdtlv/whdtlv.h` first and leave the old facade as a temporary compatibility shim.

## Final Response Required From Agent

When complete, provide a concise summary with:

1. Files/folders moved.
2. Public header decision made.
3. Build files updated.
4. Tests/builds run and results.
5. Any files intentionally left in old locations and why.
6. Any follow-up work recommended before Amiga-side testing.
