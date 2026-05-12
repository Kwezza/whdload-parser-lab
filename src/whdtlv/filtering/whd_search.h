/* filtering/whd_search.h - User search pre-filter for group candidate selection
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implements a group-level search pre-filter that runs after tlv_group_build()
 * and before tlv_select_run().  The filter narrows the set of candidate game
 * groups using a simple wildcard/substring pattern.  Profile scoring still
 * decides the winning variant inside each matched group.
 *
 * Matching rules:
 *   - No '*' or '?' in pattern: case-insensitive substring search.
 *   - '*' matches zero or more characters.
 *   - '?' matches exactly one character.
 *   - All matching is case-insensitive (ASCII-only; no locale dependency).
 *
 * New TLV path (has_group_map && used_group_id_field):
 *   Pattern matched against canonical group name from block 0x02,
 *   looked up via tlv_runtime_group_name().
 *
 * Old TLV fallback path:
 *   Pattern matched against base group name in WhdVariantGroup.group_name,
 *   derived from display_name by the existing heuristic.
 *
 * The allow list is indexed by 0-based group index into WhdGroupSet.groups[].
 * group_id values are never modified.
 *
 * C89-compatible; vbcc-safe.
 * - No heap inside the matcher.
 * - No regex.
 * - No locale-dependent functions.
 * - No recursion.
 */

#ifndef FILTERING_WHD_SEARCH_H
#define FILTERING_WHD_SEARCH_H

#include "platform.h"
#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_group.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* Search request                                                         */

/* Flags for WhdSearchRequest.flags */
#define WHD_SEARCHF_ENABLED          0x0001u  /* search is active               */
#define WHD_SEARCHF_CASE_INSENSITIVE 0x0002u  /* always on; reserved for future */
#define WHD_SEARCHF_GROUP_NAME       0x0004u  /* match canonical group map name  */
#define WHD_SEARCHF_DISPLAY_NAME     0x0008u  /* match display/base name         */

typedef struct WhdSearchRequest {
    const char   *pattern; /* wildcard pattern or substring; must not be NULL */
    unsigned int  flags;   /* WHD_SEARCHF_* bitmask                           */
} WhdSearchRequest;

/*------------------------------------------------------------------------*/
/* Group allow list                                                       */

/* Indexed by 0-based group index into WhdGroupSet.groups[].
 * group_id values are never modified or renumbered by search. */
typedef struct WhdGroupAllowList {
    uint8_t      *flags;         /* heap-allocated; flags[i] = 1 if allowed */
    unsigned long count;         /* == WhdGroupSet.group_count               */
    unsigned long matched_count; /* number of groups that passed the pattern */
} WhdGroupAllowList;

/*------------------------------------------------------------------------*/
/* API                                                                    */

/* Match a single name against the search pattern.
 *
 * Pattern rules:
 *   - No '*' or '?': case-insensitive substring search.
 *   - '*': match zero or more characters.
 *   - '?': match exactly one character.
 *   - Matching uses ASCII case folding only (no locale dependency).
 *
 * Returns 1 on match, 0 on no match.
 * NULL pattern or NULL name returns 0.
 * Empty pattern returns 1 (matches everything). */
int whd_search_match_name(const char *pattern, const char *name);

/* Build a WhdGroupAllowList from a search request over a WhdGroupSet.
 *
 * New TLV path (gs->used_group_id_field && rt->has_group_map):
 *   Matches pattern against the canonical group name from block 0x02,
 *   looked up via tlv_runtime_group_name().  Falls back to group_name
 *   if the group_id is absent from the map.
 *
 * Old TLV fallback path (no group map):
 *   Matches pattern against WhdVariantGroup.group_name (base name derived
 *   from display_name by the existing heuristic in group_util.c).
 *
 * out->flags is heap-allocated; caller must call whd_group_allow_list_free()
 * when done.  out->flags is NULL on empty group set (WHD_FILTER_OK returned).
 *
 * Returns WHD_FILTER_OK or a negative error code. */
int whd_search_build_group_allow_list(
        const TlvRuntime       *rt,
        const WhdGroupSet      *gs,
        const WhdSearchRequest *req,
        WhdGroupAllowList      *out);

/* Test whether a group index is allowed by the allow list.
 *
 * NULL allow list: returns 1 (all groups allowed; no filter active).
 * group_index >= al->count: returns 0 (out-of-range index).
 * Returns 1 if allowed, 0 if not. */
int whd_group_allowed(const WhdGroupAllowList *al, unsigned long group_index);

/* Release all memory owned by al.  Does not free al itself.
 * Safe to call on a zero-initialised struct. */
void whd_group_allow_list_free(WhdGroupAllowList *al);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_WHD_SEARCH_H */
/* End of Text */
