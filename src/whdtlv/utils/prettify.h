/*------------------------------------------------------------------------*/
/*                                                                        *
 * prettify.h — Pure C89 cross-portable name beautification system       *
 * WHDLoad Manager - Single-source title prettification                  *
 *                                                                        *
 * Copyright © 2025 Kerry Thompson                                        *
 * SPDX-License-Identifier: MIT                                           *
 * $Id$                                                                   *
 *                                                                        */
/*------------------------------------------------------------------------*/

#ifndef UTILS_PRETTIFY_H
#define UTILS_PRETTIFY_H

#include "platform.h"
#include <stddef.h>  /* for size_t */

#include <stdbool.h>

/*------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                    */
/*------------------------------------------------------------------------*/

/**
 * @brief Initialize the prettify system
 *
 * Loads the name_overrides.csv file and builds an internal linked list
 * for override lookup. Memory is allocated with standard malloc().
 *
 * @param csv_path Path to the name_overrides.csv file (may be NULL)
 * @return bool true on success, false on failure
 */
bool whdtlv_prettify_init(const char *csv_path);

/**
 * @brief Get pretty name for a raw WHDLoad pack name
 *
 * This function implements the prettification strategy:
 * 1. Check linked list for CSV override
 * 2. Apply heuristic transformation (underscores to spaces, capitalize)
 * 3. Copy result to provided buffer
 *
 * @param raw Raw pack name (e.g., "Lemmings_v1.0")
 * @param buf Output buffer for pretty name
 * @param cap Capacity of output buffer
 * @return char* Pointer to buf on success, NULL on failure
 */
char *whdtlv_prettify_title(const char *raw, char *buf, size_t cap);

/**
 * @brief Free all resources used by the prettify system
 *
 * Releases all memory allocated by whdtlv_prettify_init(). Safe to call
 * multiple times.
 */
void whdtlv_prettify_shutdown(void);

#endif /* UTILS_PRETTIFY_H */

/* End of Text */
