/* crc32.c - CRC-32/ISO-HDLC implementation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Table-driven CRC-32 using the standard reflected polynomial 0xEDB88320
 * (ISO-HDLC / ITU-T V.42).  The 256-entry table is generated once at first
 * use and reused for all subsequent calls.
 *
 * C89-compatible; no external dependencies beyond <stdint.h> and <stddef.h>.
 */

#include <utils/crc32.h>

/*------------------------------------------------------------------------*/
/* Lookup Table */

#define CRC32_POLY 0xEDB88320UL

static uint32_t s_crc32_table[256];
static int      s_crc32_table_ready = 0;

static void crc32_build_table(void)
{
    uint32_t i;
    uint32_t j;
    uint32_t val;

    for (i = 0; i < 256U; i++) {
        val = i;
        for (j = 0; j < 8U; j++) {
            if (val & 1U) {
                val = (val >> 1) ^ CRC32_POLY;
            } else {
                val >>= 1;
            }
        }
        s_crc32_table[i] = val;
    }
    s_crc32_table_ready = 1;
}

/*------------------------------------------------------------------------*/
/* Public API */

uint32_t whdtlv_crc32_init(void)
{
    if (!s_crc32_table_ready) {
        crc32_build_table();
    }
    return 0xFFFFFFFFUL;
}

uint32_t whdtlv_crc32_update(uint32_t crc, const unsigned char *data, size_t len)
{
    size_t i;
    if (!data) {
        return crc;
    }
    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ s_crc32_table[(crc ^ data[i]) & 0xFFU];
    }
    return crc;
}

uint32_t whdtlv_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

/* End of Text */
