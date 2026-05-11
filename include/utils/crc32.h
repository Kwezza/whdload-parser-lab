/* crc32.h - CRC-32/ISO-HDLC computation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Lightweight table-driven CRC-32 (polynomial 0xEDB88320, ISO-HDLC).
 * C89-compatible; suitable for both host and vbcc Amiga builds.
 *
 * Usage:
 *   uint32_t crc = whdtlv_crc32_init();
 *   crc = whdtlv_crc32_update(crc, data, len);
 *   crc = whdtlv_crc32_finalize(crc);
 */

#ifndef UTILS_CRC32_H
#define UTILS_CRC32_H

#include <stddef.h>
#include <stdint.h>

/*------------------------------------------------------------------------*/
/* CRC-32 API */

/**
 * @brief Return the initial CRC accumulator value.
 */
uint32_t whdtlv_crc32_init(void);

/**
 * @brief Feed a block of bytes into the CRC accumulator.
 * @param crc   Current accumulator (from whdtlv_crc32_init or a previous whdtlv_crc32_update).
 * @param data  Pointer to the bytes to process.
 * @param len   Number of bytes.
 * @return Updated accumulator.
 */
uint32_t whdtlv_crc32_update(uint32_t crc, const unsigned char *data, size_t len);

/**
 * @brief Finalise the CRC and return the 32-bit digest.
 * @param crc   Accumulator after all whdtlv_crc32_update calls.
 * @return Final CRC-32 value.
 */
uint32_t whdtlv_crc32_finalize(uint32_t crc);

#endif /* UTILS_CRC32_H */

/* End of Text */
