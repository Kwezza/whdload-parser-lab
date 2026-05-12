#ifndef DAT_PARSER_MINIMAL_H
#define DAT_PARSER_MINIMAL_H

#include "platform.h"
#include <stddef.h>

/*------------------------------------------------------------------------*/
/* ROM entry: name + archive transport facts from the DAT <rom .../> tag  */

typedef struct {
    char *name;           /* heap-allocated archive filename               */
    uint32_t size_bytes;  /* value of size="" attr; 0 if missing/malformed */
    uint32_t crc32;       /* value of crc="" attr (hex); 0 if missing/bad  */
} DatRomEntry;

/*------------------------------------------------------------------------*/
/* Entry-level API (preferred for new callers)                             */

size_t parse_dat_entries_minimal(const char *dat_path, DatRomEntry **out_entries);
void free_dat_entries_minimal(DatRomEntry *entries, size_t count);

/*------------------------------------------------------------------------*/
/* Legacy name-only API (superseded by parse_dat_entries_minimal)         */
/* Declarations removed in Phase 2 header hygiene (2026-05-11).          */
/* Definitions retained in dat_parser_minimal.c for compatibility.       */

#endif /* DAT_PARSER_MINIMAL_H */
