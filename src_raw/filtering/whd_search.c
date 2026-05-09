/* src_raw/filtering/whd_search.c - User search pre-filter implementation
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * See filtering/whd_search.h for the public API and design notes.
 *
 * Matching is performed in two modes:
 *
 *   Substring mode (no '*' or '?' in pattern):
 *     Case-insensitive strstr equivalent.  "lotus" matches "Lotus2".
 *
 *   Wildcard mode ('*' or '?' present):
 *     Iterative backtracking matcher.  No recursion; no heap.
 *     '*' — zero or more characters.
 *     '?' — exactly one character.
 *
 * ASCII case folding is used throughout; no locale functions are called.
 *
 * C89-compatible; vbcc-safe.
 * - All variables declared at block start.
 * - No VLAs.
 * - No for-loop initialiser declarations.
 */

#include <filtering/whd_search.h>
#include <filtering/tlv_filter.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Internal helpers                                                       */
/*========================================================================*/

/* ASCII-only lowercase conversion.  Safe on all platforms; no locale. */
static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

/* Returns 1 if pattern contains any wildcard character ('*' or '?'). */
static int has_wildcard(const char *pattern)
{
    const char *p;

    for (p = pattern; *p != '\0'; p++) {
        if (*p == '*' || *p == '?') return 1;
    }
    return 0;
}

/* Case-insensitive substring search.
 * Returns 1 if needle is found anywhere inside haystack, 0 otherwise.
 * Empty needle always matches. */
static int ci_strstr(const char *haystack, const char *needle)
{
    unsigned long  nlen;
    unsigned long  i;
    const char    *np;
    const char    *h;

    nlen = 0;
    for (np = needle; *np != '\0'; np++) nlen++;
    if (nlen == 0) return 1;

    for (h = haystack; *h != '\0'; h++) {
        for (i = 0; i < nlen; i++) {
            if (h[i] == '\0') break;
            if (ascii_lower((unsigned char)h[i]) !=
                ascii_lower((unsigned char)needle[i])) break;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

/* Case-insensitive iterative wildcard match.
 *   '*' — zero or more characters
 *   '?' — exactly one character
 *
 * Uses backtrack state (star_pat, star_str) to avoid recursion.
 * Returns 1 on match, 0 on no match. */
static int wildcard_match(const char *pat, const char *str)
{
    const char *star_pat;  /* position of last '*' in pat               */
    const char *star_str;  /* str position when that '*' was first seen  */
    int         pc;
    int         sc;

    star_pat = NULL;
    star_str = NULL;

    while (*str != '\0') {
        pc = ascii_lower((unsigned char)*pat);
        sc = ascii_lower((unsigned char)*str);

        if (*pat == '*') {
            /* Record '*' location; do not advance str yet —
             * '*' may match zero characters.                            */
            star_pat = pat;
            star_str = str;
            pat++;
        } else if (*pat == '?' || pc == sc) {
            /* Literal or single-char wildcard: advance both cursors.    */
            pat++;
            str++;
        } else if (star_pat != NULL) {
            /* Mismatch but a previous '*' can absorb one more char.
             * Retry with pat reset to just after the '*'.               */
            star_str++;
            str = star_str;
            pat = star_pat + 1;
        } else {
            return 0;
        }
    }

    /* Consume any trailing '*'s (they match the empty suffix).           */
    while (*pat == '*') pat++;

    return (*pat == '\0') ? 1 : 0;
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int whd_search_match_name(const char *pattern, const char *name)
{
    if (pattern == NULL || name == NULL) return 0;
    if (*pattern == '\0') return 1;  /* empty pattern matches everything  */

    if (has_wildcard(pattern)) {
        return wildcard_match(pattern, name);
    }
    return ci_strstr(name, pattern);
}

int whd_search_build_group_allow_list(
        const TlvRuntime       *rt,
        const WhdGroupSet      *gs,
        const WhdSearchRequest *req,
        WhdGroupAllowList      *out)
{
    uint8_t       *flags;
    unsigned long  i;
    const char    *name;
    int            matched;

    if (rt == NULL || gs == NULL || req == NULL || out == NULL) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    out->flags         = NULL;
    out->count         = 0;
    out->matched_count = 0;

    if (gs->group_count == 0) {
        return WHD_FILTER_OK;
    }

    flags = (uint8_t *)malloc(gs->group_count * sizeof(uint8_t));
    if (flags == NULL) {
        return WHD_FILTER_ERR_OOM;
    }
    memset(flags, 0, gs->group_count * sizeof(uint8_t));

    for (i = 0; i < gs->group_count; i++) {
        name = NULL;

        /* New TLV path: use canonical name from group map block 0x02.
         * tlv_runtime_group_name() uses the existing big-endian-safe
         * lookup; group_id is not modified.                            */
        if (gs->used_group_id_field && rt->has_group_map) {
            name = tlv_runtime_group_name(rt, gs->groups[i].group_id);
        }

        /* Old TLV / map-miss fallback: use base name from display_name. */
        if (name == NULL) {
            name = gs->groups[i].group_name;
        }

        matched = (name != NULL) ? whd_search_match_name(req->pattern, name) : 0;
        flags[i] = matched ? 1u : 0u;
        if (matched) out->matched_count++;
    }

    out->flags = flags;
    out->count = gs->group_count;
    return WHD_FILTER_OK;
}

int whd_group_allowed(const WhdGroupAllowList *al, unsigned long group_index)
{
    if (al == NULL)            return 1;  /* no filter: all allowed        */
    if (al->count == 0)        return 1;  /* empty list: all allowed       */
    if (group_index >= al->count) return 0;
    return (al->flags[group_index] != 0u) ? 1 : 0;
}

void whd_group_allow_list_free(WhdGroupAllowList *al)
{
    if (al == NULL) return;
    if (al->flags != NULL) {
        free(al->flags);
        al->flags = NULL;
    }
    al->count         = 0;
    al->matched_count = 0;
}

/* End of Text */
