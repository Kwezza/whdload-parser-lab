/*------------------------------------------------------------------------*/
/*                                                                        *
 * platform.h — Platform abstraction layer for dual-target builds       *
 * WHDLoad Manager - Support both Amiga and host platforms               *
 *                                                                        *
 * Copyright © 2025 Kerry Thompson                                        *
 * SPDX-License-Identifier: MIT                                           *
 *                                                                        */
/*------------------------------------------------------------------------*/

#ifndef PLATFORM_H
#define PLATFORM_H

/* Include platform I/O wrappers */
#include "platform/platform_io.h"
/* Include memory allocation wrappers */
#include "platform/platform_mem.h"

/*------------------------------------------------------------------------*/
/* Platform-specific includes and definitions                            */
/*------------------------------------------------------------------------*/

#ifdef PLATFORM_AMIGA
    /* Real Amiga target - use AmigaOS APIs */
    /* The AmigaOS headers are now properly included via VBCC */
    /* All types and functions come from the real SDK headers */

    /* Include standard C headers for compatibility */
    #include <stdlib.h>
    #include <stdint.h>
    #include <stdbool.h>

    /* Memory allocation macros */
    #define WHD_ALLOC(sz)  AllocVec((sz), MEMF_PUBLIC)
    #define WHD_FREE(p)    FreeVec(p)

    /* Struct packing for vbcc */
    #define WHD_PACKED     /* vbcc doesn't support __attribute__((packed)) */

    /* Path and text formatting */
    #define WHD_PATH_SEP   '/'
    #define WHD_PATH_SEP_STR "/"
    #define WHD_EOL        "\r\n"

#else /* PLATFORM_HOST */
    /* Host target - Linux/Windows/Raspberry Pi tools */
    #include <stdlib.h>
    #include <stdint.h>
    #include <stdbool.h>
    #include <string.h>

    /* Legacy Amiga-style types for host build - marked deprecated to force migration */
    typedef uint8_t         UBYTE   __attribute__((deprecated("use uint8_t")));
    typedef uint16_t        UWORD   __attribute__((deprecated("use uint16_t")));
    typedef uint32_t        ULONG   __attribute__((deprecated("use uint32_t or size_t")));
    typedef char            BYTE;
    typedef short           WORD;
    typedef long            LONG;
    typedef bool            BOOL;   /* Will migrate to bool later */
    typedef void           *APTR;
    typedef char           *STRPTR  __attribute__((deprecated("use char *")));
    typedef const char     *CONST_STRPTR;
    typedef void            VOID;
    typedef void           *BPTR;

    /* DOS constants for host build */
    #define MODE_OLDFILE   0
    #define MODE_NEWFILE   1
    #define OFFSET_CURRENT 0
    #define RETURN_OK      0
    #define RETURN_FAIL    20

    /* Stub functions for host builds that use Amiga I/O */
    #define Open(name, mode)        NULL
    #define Close(file)             ((void)0)
    #define Read(file, buffer, length)  0
    #define Write(file, buffer, length) 0
    #define Seek(file, pos, mode)   0
    #define FGets(file, buffer, len) NULL
    #define CopyMem(src, dst, len)  memcpy(dst, src, len)

    /* Boolean constants for host build */
    #ifndef TRUE
    #define TRUE  1
    #endif
    #ifndef FALSE
    #define FALSE 0
    #endif

    /* Memory allocation macros */
    #define WHD_ALLOC(sz)  malloc(sz)
    #define WHD_FREE(p)    free(p)

    /* Struct packing for host compilers */
    #define WHD_PACKED     __attribute__((packed))

    /* Path and text formatting */
    #ifdef _WIN32
        #define WHD_PATH_SEP   '\\'
        #define WHD_PATH_SEP_STR "\\"
    #else
        #define WHD_PATH_SEP   '/'
        #define WHD_PATH_SEP_STR "/"
    #endif
    #define WHD_EOL        "\n"

    /* Host builds are supported for testing and utilities */
    /* Main application will exit with platform error as intended */

#endif /* PLATFORM_AMIGA */

/*------------------------------------------------------------------------*/
/* Common utility macros                                                   */
/*------------------------------------------------------------------------*/

/* Mark an intentionally unused variable/parameter without vbcc warnings */
#ifndef WHD_UNUSED
#define WHD_UNUSED(x) do { if (0) { (void)(x); } } while (0)
#endif

#endif /* PLATFORM_H */

/* End of Text */
