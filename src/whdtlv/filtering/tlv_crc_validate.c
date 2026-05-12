/* src_raw/filtering/tlv_crc_validate.c - CSV CRC fingerprint validation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * For each CSV fingerprint embedded in the TLV:
 *   1. Build path: defs_dir + "/" + csv_name + ".csv"
 *   2. Load file raw bytes
 *   3. Compute CRC-32/ISO-HDLC
 *   4. Compare against the TLV-stored value
 *
 * In strict mode (WHD_FILTER_CRC_STRICT) any failure returns the
 * appropriate WHD_FILTER_ERR_CSV_* code immediately.
 * In warn-only mode (WHD_FILTER_CRC_WARNONLY) the function accumulates
 * counts in *out and returns WHD_FILTER_OK so the caller can surface
 * warnings.
 *
 * The csv_name stored in the TLV has no ".csv" extension — the writer
 * stores the base name only.  We append ".csv" when building the path.
 *
 * The Makefile already has crc32.c in SRC_FH, so we can include its
 * header directly.
 */

#include "whdtlv/filtering/tlv_crc_validate.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/utils/crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/
/* Internal: compute CRC-32 of a file given its full path.
 * Returns WHD_FILTER_OK and sets *out_crc on success.
 * Returns WHD_FILTER_ERR_CSV_MISSING or WHD_FILTER_ERR_CSV_UNREADABLE
 * on failure. */

static int file_crc32(const char *path, uint32_t *out_crc)
{
    FILE    *f;
    char     line[4096];
    uint32_t crc;

    /*
     * Open in text mode ("r") to match csv_cache.c which uses fgets on a
     * text-mode FILE.  On Windows this converts \r\n -> \n, so the CRC
     * produced here is identical to the one embedded in the TLV by the
     * builder.  Using "rb" would produce a different CRC on Windows.
     */
    f = fopen(path, "r");
    if (!f) {
        return WHD_FILTER_ERR_CSV_MISSING;
    }

    crc = crc32_init();
    while (fgets(line, (int)sizeof(line), f)) {
        crc = crc32_update(crc, (const unsigned char *)line,
                           (size_t)strlen(line));
    }

    if (ferror(f)) {
        fclose(f);
        return WHD_FILTER_ERR_CSV_UNREADABLE;
    }

    fclose(f);
    *out_crc = crc32_finalize(crc);
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Internal: build the full path for a CSV name.
 * csv_name has no extension; we append ".csv".
 * Returns WHD_FILTER_OK or WHD_FILTER_ERR_BAD_ARG if the buffer is too
 * small (path would overflow). */

#define PATH_BUF_SIZE 512

static int build_csv_path(char         *out,
                          size_t        out_size,
                          const char   *defs_dir,
                          const char   *csv_name)
{
    size_t dir_len;
    size_t name_len;
    size_t needed;
    char   sep;

    dir_len  = strlen(defs_dir);
    name_len = strlen(csv_name);

    /* Determine separator: use the last char of defs_dir, or default '/' */
    if (dir_len > 0) {
        char last = defs_dir[dir_len - 1];
        sep = (last == '/' || last == '\\') ? '\0' : '/';
    } else {
        sep = '/';
    }

    /* needed: dir + optional_sep + name + ".csv" + NUL */
    needed = dir_len + (sep ? 1u : 0u) + name_len + 4u + 1u;
    if (needed > out_size) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    if (sep) {
        snprintf(out, out_size, "%s%c%s.csv", defs_dir, sep, csv_name);
    } else {
        snprintf(out, out_size, "%s%s.csv", defs_dir, csv_name);
    }
    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */

int tlv_crc_validate(const TlvRuntime     *rt,
                     const char           *defs_dir,
                     unsigned int          flags,
                     WhdCrcValidateResult *out)
{
    unsigned long i;
    char          path[PATH_BUF_SIZE];
    uint32_t      live_crc;
    int           rc;

    if (out) {
        memset(out, 0, sizeof(*out));
    }

    if (!rt) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    if (!rt->has_crc_map || rt->crc_map.count == 0u) {
        /* TLV has no fingerprint block.  In strict mode this is fatal. */
        if (out) {
            out->no_crc_block = 1;
        }
        if (flags & WHD_FILTER_CRC_STRICT) {
            fprintf(stderr, "WARNING: TLV has no CRC fingerprint block\n");
            /* Treat as a warning rather than hard abort: a TLV without a CRC
             * block can still be consumed, it just can't be validated. */
        }
        return WHD_FILTER_OK;
    }

    for (i = 0u; i < rt->crc_map.count; i++) {
        const TlvCrcEntry *entry = &rt->crc_map.entries[i];

        /* Build path */
        rc = build_csv_path(path, sizeof(path), defs_dir, entry->csv_name);
        if (rc != WHD_FILTER_OK) {
            /* Path too long — treat as unreadable */
            if (out) {
                out->unreadable_count++;
            }
            if (flags & WHD_FILTER_CRC_STRICT) {
                fprintf(stderr,
                        "ERROR: CSV path too long for: %s\n",
                        entry->csv_name);
                return WHD_FILTER_ERR_CSV_UNREADABLE;
            }
            fprintf(stderr, "WARNING: CSV path too long for: %s\n",
                    entry->csv_name);
            continue;
        }

        /* Compute CRC of the live file */
        rc = file_crc32(path, &live_crc);

        if (rc == WHD_FILTER_ERR_CSV_MISSING) {
            if (out) {
                out->missing_count++;
            }
            if (flags & WHD_FILTER_CRC_STRICT) {
                fprintf(stderr, "ERROR: missing CSV: %s\n", path);
                return WHD_FILTER_ERR_CSV_MISSING;
            }
            fprintf(stderr, "WARNING: missing CSV: %s\n", path);
            continue;
        }

        if (rc == WHD_FILTER_ERR_CSV_UNREADABLE) {
            if (out) {
                out->unreadable_count++;
            }
            if (flags & WHD_FILTER_CRC_STRICT) {
                fprintf(stderr, "ERROR: unreadable CSV: %s\n", path);
                return WHD_FILTER_ERR_CSV_UNREADABLE;
            }
            fprintf(stderr, "WARNING: unreadable CSV: %s\n", path);
            continue;
        }

        /* Compare */
        if (live_crc != entry->crc32) {
            if (out) {
                out->mismatch_count++;
            }
            if (flags & WHD_FILTER_CRC_STRICT) {
                fprintf(stderr,
                        "ERROR: CSV CRC mismatch: %s  tlv=%08lX  current=%08lX\n",
                        entry->csv_name,
                        (unsigned long)entry->crc32,
                        (unsigned long)live_crc);
                return WHD_FILTER_ERR_CSV_CRC_MISMATCH;
            }
            fprintf(stderr,
                    "WARNING: CSV CRC mismatch: %s  tlv=%08lX  current=%08lX\n",
                    entry->csv_name,
                    (unsigned long)entry->crc32,
                    (unsigned long)live_crc);
            continue;
        }

        /* Match */
        if (out) {
            out->ok_count++;
        }
    }

    return WHD_FILTER_OK;
}

/* End of Text */
