# Source Layout

This document describes the repository folder structure after the May 2026 cleanup.

```
assets_raw/      Editable source assets: DAT files, CSV definitions, profiles and prefs.
tools_src/       Standalone command-line tools, demos and harness entry points.
src/whdtlv/      Reusable WHDTLV implementation code.
include/whdtlv/  Public facade used by external callers.
output/          Generated TLV files and result files.
build/           Host and Amiga build products.
```

## Detailed Structure

```
assets_raw/
    defs/           CSV field definitions used by the pipeline
    prefs/          pack_types.ini and other runtime prefs
    profiles/       Filter profile files
    Dats/           WHDLoad DAT input files

build/
    amiga/          vbcc Amiga build products
    host/           GCC host build products

docs/               Documentation and plans

include/
    platform.h      Global platform abstraction header (included everywhere as "platform.h")
    whdtlv/
        whdtlv.h    Public facade — the only header external callers need to include

notes/              Working notes and backport inventory

output/             Generated TLV output files

src/
    whdtlv/
        core/       TLV construction, metadata parsing, variant model internals
        filtering/  Filter pipeline, profile binding, runtime scoring and selection
        io/         File I/O helpers: logging, pack type loading
        platform/   Host/Amiga portability layer: platform_io, platform_string, platform_mem
        utils/      Small reusable helpers: crc32, prettify

tools_src/          Entry points for command-line tools
    dat_to_tlv_main.c       Main DAT-to-TLV builder tool
```

## Include Path Policy

The build system provides two include roots:

```makefile
-Iinclude
-Isrc
```

Source files use stable namespaced includes:

```c
#include "platform.h"
#include "whdtlv/whdtlv.h"
#include "whdtlv/core/tlv_builder.h"
#include "whdtlv/filtering/filter_runtime.h"
#include "whdtlv/io/writeLog.h"
#include "whdtlv/platform/platform_io.h"
#include "whdtlv/utils/crc32.h"
```

No `../` relative includes are used.

## Vendoring Into Another Project

`src/whdtlv/` is intentionally self-contained and namespaced so it can be copied
into another project (such as WHDFetch) as a single folder without colliding with
that project's own source tree. The only additional requirement is that the host
project provides `include/platform.h` and adds `-Iinclude -Isrc` to its CFLAGS.
