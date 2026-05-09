/* src_raw/group_util.c - Canonical group-name derivation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * See include_raw/group_util.h for the derivation rule.
 *
 * C89-compatible; vbcc-safe.
 * - Variables declared at block top.
 * - No VLAs, no for-loop init declarations.
 * - No dynamic allocation.
 */

#include <group_util.h>
#include <string.h>

unsigned long derive_group_name(const char    *display_name,
                                char          *out,
                                unsigned long  out_size)
{
    unsigned long full_len;
    unsigned long base_len;
    unsigned long copy_len;
    unsigned long i;

    if (!out || out_size == 0u) {
        return 0u;
    }
    out[0] = '\0';

    if (!display_name || display_name[0] == '\0') {
        return 0u;
    }

    /* Compute full length without strlen to stay explicit. */
    full_len = 0u;
    while (display_name[full_len]) {
        full_len++;
    }

    /* Find first "_v<digit>" marker (case-insensitive 'v').
     * i starts at 1 so prev = display_name[i-1] is always valid.        */
    base_len = full_len;
    for (i = 1u; i < full_len; i++) {
        char prev = display_name[i - 1u];
        char c    = display_name[i];
        if (prev == '_' && (c == 'v' || c == 'V')) {
            if (i + 1u < full_len) {
                char next = display_name[i + 1u];
                if (next >= '0' && next <= '9') {
                    /* Found "_v<digit>" — group name ends before '_'. */
                    base_len = i - 1u;
                    break;
                }
            }
        }
    }

    /* Guard: never emit an empty group name.
     * This can happen when the name itself starts with "_v1...".          */
    if (base_len == 0u) {
        base_len = full_len;
    }

    /* Copy at most out_size-1 characters and NUL-terminate. */
    copy_len = (base_len < out_size - 1u) ? base_len : (out_size - 1u);
    memcpy(out, display_name, copy_len);
    out[copy_len] = '\0';

    return copy_len;
}
