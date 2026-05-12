/* src_raw/filtering/profile_binder.c - .profile parser and TLV field binder
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Parses a `.profile` INI file and produces a WhdBoundProfile that maps
 * each filter field to its TLV field ID and resolves include/exclude token
 * lists to numeric IDs.
 *
 * Token resolution order:
 *   1. Case-insensitive lookup in the CSV definition file for the field.
 *      The CSV name is located by scanning the TlvRuntime CRC map for a
 *      case-insensitive match against the field name.
 *   2. If no CSV is found, or the token is absent from the CSV, fall back
 *      to an FNV-1a 8-bit hash of the lowercased token string.  The TLV
 *      builder uses the same fallback, so hash-matched tokens compare equal
 *      at scoring time.
 *
 * CSV file format (one row per line):
 *   <numeric_id>,<token>,<description>[,default]
 *
 * C89-compatible; vbcc-safe.
 * - All variables declared at the top of their enclosing block.
 * - No VLAs.
 * - No for-loop init declarations.
 * - No C99-only syntax.
 * - snprintf used (supported by vbcc as a C99 extension).
 */

#include "whdtlv/filtering/profile_binder.h"
#include "whdtlv/filtering/tlv_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*========================================================================*/
/* Internal helpers                                                       */
/*========================================================================*/

/*------------------------------------------------------------------------*/
/* FNV-1a 8-bit hash (lowercased)                                        */

static unsigned char token_hash8(const char *tok)
{
    const unsigned char *p;
    uint32_t h = 2166136261u; /* 32-bit FNV-1a basis */
    for (p = (const unsigned char *)tok; *p; p++) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - 'A' + 'a');
        }
        h ^= c;
        h *= 16777619u; /* FNV prime */
    }
    h ^= (h >> 16);
    h ^= (h >> 8);
    return (unsigned char)(h & 0xFFu);
}

/*------------------------------------------------------------------------*/
/* String utilities                                                       */

static void pb_trim(char *s)
{
    char *p;
    char *e;
    if (!s) {
        return;
    }
    p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    e = s + strlen(s);
    while (e > s) {
        char c = *(e - 1);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            e--;
            *e = '\0';
        } else {
            break;
        }
    }
}

static int pb_strcasecmp(const char *a, const char *b)
{
    for (;;) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == '\0') {
            return 0;
        }
        a++;
        b++;
    }
}

/*------------------------------------------------------------------------*/
/* Find the CSV base name for a field by scanning the TLV CRC map.
 * Uses case-insensitive matching so "language" finds "Language".
 * Returns 1 and fills out_csv_name (64 bytes) if found, else 0.         */

static int find_csv_for_field(const TlvRuntime *rt,
                              const char       *field_name,
                              char             *out_csv_name)
{
    unsigned long i;
    if (!rt->has_crc_map) {
        return 0;
    }
    for (i = 0u; i < rt->crc_map.count; i++) {
        if (pb_strcasecmp(rt->crc_map.entries[i].csv_name, field_name) == 0) {
            strncpy(out_csv_name, rt->crc_map.entries[i].csv_name, 63);
            out_csv_name[63] = '\0';
            return 1;
        }
    }
    return 0;
}

/*------------------------------------------------------------------------*/
/* Build the full path for a CSV file.
 * Returns 1 on success, 0 if the path would overflow the buffer.        */

#define PB_PATH_MAX 512

static int build_csv_path(char       *out,
                          const char *defs_dir,
                          const char *csv_name)
{
    size_t dir_len = strlen(defs_dir);
    char   sep;
    if (dir_len > 0) {
        char last = defs_dir[dir_len - 1];
        sep = (last == '/' || last == '\\') ? '\0' : '/';
    } else {
        sep = '/';
    }
    if (sep) {
        snprintf(out, PB_PATH_MAX, "%s%c%s.csv", defs_dir, sep, csv_name);
    } else {
        snprintf(out, PB_PATH_MAX, "%s%s.csv", defs_dir, csv_name);
    }
    return 1;
}

/*------------------------------------------------------------------------*/
/* Look up a single token in a CSV file.
 * CSV format: <id>,<token>,<description>[,default]
 * Matching is case-insensitive on the token column.
 * Sets *out_id to the numeric ID and returns 1 if found, else 0.        */

static int csv_lookup_token(const char *defs_dir,
                            const char *csv_name,
                            const char *token,
                            uint32_t   *out_id)
{
    char  path[PB_PATH_MAX];
    FILE *f;
    char  line[256];

    build_csv_path(path, defs_dir, csv_name);
    f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        char    *p;
        uint32_t id;
        char    *tok_start;
        char    *comma;

        pb_trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') {
            continue;
        }
        id = (uint32_t)strtoul(line, &p, 10);
        if (p == line || *p != ',') {
            continue;
        }
        p++; /* skip comma after id */

        /* token field is everything up to the next comma */
        tok_start = p;
        comma = strchr(p, ',');
        if (comma) {
            *comma = '\0';
        }
        pb_trim(tok_start);

        if (pb_strcasecmp(tok_start, token) == 0) {
            fclose(f);
            *out_id = id;
            return 1;
        }
        if (comma) {
            *comma = ','; /* restore for next parse pass */
        }
    }

    fclose(f);
    return 0;
}

/*------------------------------------------------------------------------*/
/* Scan a CSV file for a row with "default" in column 4.
 * Returns the numeric ID of the default row, or 0 if none.              */

static uint32_t csv_find_default(const char *defs_dir, const char *csv_name)
{
    char  path[PB_PATH_MAX];
    FILE *f;
    char  line[256];

    build_csv_path(path, defs_dir, csv_name);
    f = fopen(path, "r");
    if (!f) {
        return 0u;
    }

    while (fgets(line, sizeof(line), f)) {
        char    *p;
        uint32_t id;

        pb_trim(line);
        if (!line[0] || line[0] == '#') {
            continue;
        }
        id = (uint32_t)strtoul(line, &p, 10);
        if (p == line || *p != ',') {
            continue;
        }
        /* Quick check: does this line contain ",default" anywhere? */
        if (strstr(p, ",default")) {
            fclose(f);
            return id;
        }
    }

    fclose(f);
    return 0u;
}

/*------------------------------------------------------------------------*/
/* Resolve a comma-separated token list into numeric IDs.
 * has_csv / csv_name control whether CSV lookup is attempted.
 * Returns the number of IDs written into ids[] (at most max).           */

static uint8_t resolve_token_list(const char *val,
                                  uint16_t   *ids,
                                  uint8_t     max,
                                  const char *defs_dir,
                                  const char *csv_name,
                                  int         has_csv)
{
    char    buf[512];
    char   *p;
    uint8_t count = 0;

    if (!val || !*val) {
        return 0u;
    }
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    p = buf;

    while (*p && count < max) {
        char    *start;
        char    *comma;
        uint32_t csv_id = 0;
        uint16_t store_id;

        while (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }

        start = p;
        comma = strchr(p, ',');
        if (comma) {
            *comma = '\0';
        }
        pb_trim(start);

        if (*start) {
            if (has_csv && defs_dir && csv_name) {
                csv_lookup_token(defs_dir, csv_name, start, &csv_id);
            }
            if (csv_id) {
                store_id = (uint16_t)(csv_id & 0xFFFFu);
            } else {
                store_id = (uint16_t)token_hash8(start);
            }
            ids[count++] = store_id;
        }

        if (comma) {
            p = comma + 1;
        } else {
            break;
        }
    }
    return count;
}

/*------------------------------------------------------------------------*/
/* Find or create a WhdBoundField slot for field_name.
 * Returns a pointer into out->fields, or NULL if PB_MAX_FIELDS reached. */

static WhdBoundField *ensure_bound_field(WhdBoundProfile *out,
                                         const char      *field_name,
                                         uint8_t          tlv_field_id)
{
    WhdBoundField *bf;
    uint8_t        i;
    size_t         r;

    for (i = 0u; i < out->field_count; i++) {
        if (strcmp(out->fields[i].field_name, field_name) == 0) {
            return &out->fields[i];
        }
    }
    if (out->field_count >= PB_MAX_FIELDS) {
        return NULL;
    }
    bf = &out->fields[out->field_count++];
    memset(bf, 0, sizeof(*bf));
    for (r = 0u; r < 256u; r++) {
        bf->rank_by_id[r] = 0xFF;
    }
    strncpy(bf->field_name, field_name, sizeof(bf->field_name) - 1);
    bf->tlv_field_id = tlv_field_id;
    return bf;
}

/*------------------------------------------------------------------------*/
/* Apply a resolved include list to a bound field.
 * Updates rank_by_id table and fills include_ids[].                     */

static void apply_include_ids(WhdBoundField  *bf,
                               const uint16_t *ids,
                               uint8_t         count)
{
    uint8_t k;
    for (k = 0u; k < count && bf->include_count < PB_MAX_TOKENS; k++) {
        uint16_t tid = ids[k];
        bf->include_ids[bf->include_count] = tid;
        bf->rank_by_id[tid & 0xFFu] = bf->include_count;
        bf->include_count++;
    }
}

/*------------------------------------------------------------------------*/
/* Resolve a bucketed include list into bf->include_ids[] and            */
/* bf->buckets[].                                                        */
/*                                                                       */
/* "/" splits the include list into buckets; "," separates tokens within */
/* each bucket.  Empty buckets (e.g. from "AGA//OCS") are skipped.      */
/*                                                                       */
/* Returns  0 on success.                                                */
/* Returns -1 if the number of non-empty buckets exceeds                 */
/*            FP_MAX_BUCKETS_FIELD (caller must treat as a hard error).  */

static int resolve_bucketed_include(const char    *val,
                                    WhdBoundField *bf,
                                    const char    *defs_dir,
                                    const char    *csv_name,
                                    int            has_csv)
{
    char     buf[512];
    char    *p;
    char    *slash;
    char    *seg;
    uint8_t  start_idx;
    uint8_t  added;
    uint8_t  room;
    uint8_t  bkt;
    uint16_t tmp_ids[PB_MAX_TOKENS];

    if (!val || !*val) {
        return 0;
    }

    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    p   = buf;
    bkt = 0;

    while (*p) {
        slash = strchr(p, '/');
        if (slash) {
            *slash = '\0';
        }

        seg = p;
        pb_trim(seg);
        start_idx = bf->include_count;

        /* resolve comma-separated tokens within this bucket segment */
        if (*seg) {
            room  = (bf->include_count < PB_MAX_TOKENS)
                    ? (uint8_t)(PB_MAX_TOKENS - bf->include_count)
                    : 0u;
            added = resolve_token_list(seg, tmp_ids, room,
                                       defs_dir, csv_name, has_csv);
            apply_include_ids(bf, tmp_ids, added);
        }

        /* record this bucket only if it produced at least one token */
        if (bf->include_count > start_idx) {
            if (bkt >= FP_MAX_BUCKETS_FIELD) {
                return -1; /* too many buckets */
            }
            bf->buckets[bkt].start = start_idx;
            bf->buckets[bkt].count = (uint8_t)(bf->include_count - start_idx);
            bkt++;
        }

        if (slash) {
            p = slash + 1;
        } else {
            break;
        }
    }

    bf->bucket_count = bkt;
    return 0;
}

/*========================================================================*/
/* Public API                                                             */
/*========================================================================*/

int whd_profile_load(const char       *path,
                     const TlvRuntime *rt,
                     const char       *defs_dir,
                     WhdBoundProfile  *out)
{
    FILE  *f;
    char   line[512];
    char   section[128];

    if (!path || !rt || !out) {
        return WHD_FILTER_ERR_BAD_ARG;
    }

    memset(out, 0, sizeof(*out));

    f = fopen(path, "r");
    if (!f) {
        return WHD_FILTER_ERR_PROFILE_LOAD;
    }

    section[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *p;
        char *eq;
        char *key;
        char *val;
        char *rb;

        pb_trim(line);
        p = line;
        if (!*p || *p == '#' || *p == ';') {
            continue;
        }

        /* -- Section header -------------------------------------------- */
        if (*p == '[') {
            rb = strchr(p, ']');
            if (rb) {
                size_t slen = (size_t)(rb - p - 1);
                if (slen >= sizeof(section)) {
                    slen = sizeof(section) - 1;
                }
                memcpy(section, p + 1, slen);
                section[slen] = '\0';
            }
            continue;
        }

        /* -- Key=value pair -------------------------------------------- */
        eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        key = p;
        val = eq + 1;
        pb_trim(key);
        pb_trim(val);

        /* == [Profile] ================================================= */
        if (strcmp(section, "Profile") == 0) {
            if (strcmp(key, "id") == 0) {
                strncpy(out->id, val, sizeof(out->id) - 1);
            } else if (strcmp(key, "name") == 0) {
                strncpy(out->name, val, sizeof(out->name) - 1);
            } else if (strcmp(key, "version") == 0) {
                out->version = (uint32_t)atoi(val);
            } else if (strcmp(key, "debug") == 0) {
                if (*val == '1' || *val == 'y' || *val == 'Y' ||
                    *val == 't' || *val == 'T') {
                    out->debug_enabled = 1;
                }
            }
            continue;
        }

        /* == [Filter.<fieldname>] ====================================== */
        if (strncmp(section, "Filter.", 7) == 0) {
            const char    *field_name = section + 7;
            uint8_t        tlv_fid   = tlv_runtime_field_id(rt, field_name);
            WhdBoundField *bf;
            char           csv_name[64];
            int            has_csv;
            uint16_t       tmp_ids[PB_MAX_TOKENS];
            uint8_t        added;
            uint32_t       def_id;

            if (!tlv_fid) {
                /* Field not present in TLV field map — unknown field */
                out->had_warnings = 1;
                continue;
            }

            bf = ensure_bound_field(out, field_name, tlv_fid);
            if (!bf) {
                out->had_warnings = 1;
                continue;
            }

            has_csv = find_csv_for_field(rt, field_name, csv_name);

            if (strcmp(key, "include") == 0 && *val) {
                if (resolve_bucketed_include(val, bf,
                                             defs_dir,
                                             has_csv ? csv_name : NULL,
                                             has_csv) < 0) {
                    fprintf(stderr,
                            "profile_binder: [Filter.%s] include= has more"
                            " than %d slash-separated buckets -- profile"
                            " rejected\n",
                            field_name, (int)FP_MAX_BUCKETS_FIELD);
                    fclose(f);
                    return WHD_FILTER_ERR_PROFILE_LOAD;
                }

                /* While we have the csv_name handy, capture the default */
                if (has_csv && !bf->has_default && defs_dir) {
                    def_id = csv_find_default(defs_dir, csv_name);
                    if (def_id) {
                        bf->has_default      = 1;
                        bf->default_token_id = (uint16_t)(def_id & 0xFFFFu);
                    }
                }
            } else if (strcmp(key, "exclude") == 0 && *val) {
                added = resolve_token_list(val, tmp_ids, PB_MAX_TOKENS,
                                           defs_dir,
                                           has_csv ? csv_name : NULL,
                                           has_csv);
                {
                    uint8_t k;
                    for (k = 0u; k < added && bf->exclude_count < PB_MAX_TOKENS; k++) {
                        bf->exclude_ids[bf->exclude_count++] = tmp_ids[k];
                    }
                }
            }
            continue;
        }

        /* == [Scoring] ================================================= */
        if (strcmp(section, "Scoring") == 0) {
            if (strncmp(key, "weight.", 7) == 0) {
                const char    *fname  = key + 7;
                uint8_t        tlv_fid = tlv_runtime_field_id(rt, fname);
                WhdBoundField *bf;
                int            w;

                if (!tlv_fid) {
                    out->had_warnings = 1;
                    continue;
                }
                bf = ensure_bound_field(out, fname, tlv_fid);
                if (!bf) {
                    out->had_warnings = 1;
                    continue;
                }
                w = atoi(val);
                if (w < 0)   { w = 0;   }
                if (w > 255) { w = 255; }
                bf->weight = (uint8_t)w;
            }
            continue;
        }
    }

    fclose(f);

    /* For fields that still have no default, do one more CSV pass.      */
    {
        uint8_t i;
        for (i = 0u; i < out->field_count; i++) {
            WhdBoundField *bf      = &out->fields[i];
            char           csv_name[64];
            uint32_t       def_id;
            if (!bf->has_default && defs_dir &&
                find_csv_for_field(rt, bf->field_name, csv_name)) {
                def_id = csv_find_default(defs_dir, csv_name);
                if (def_id) {
                    bf->has_default      = 1;
                    bf->default_token_id = (uint16_t)(def_id & 0xFFFFu);
                }
            }
        }
    }

    return WHD_FILTER_OK;
}

/*------------------------------------------------------------------------*/
/* Dump helper (harness use only — not called in Amiga runtime)          */

void whd_profile_dump(const WhdBoundProfile *p)
{
    uint8_t i;
    uint8_t k;

    if (!p) {
        return;
    }

    printf("--- Bound Profile ---\n");
    printf("  id      : %s\n", p->id[0]   ? p->id   : "(none)");
    printf("  name    : %s\n", p->name[0] ? p->name : "(none)");
    printf("  version : %lu\n", (unsigned long)p->version);
    printf("  fields  : %u\n",  (unsigned)p->field_count);
    if (p->had_warnings) {
        printf("  WARNING : had_warnings=1 (unknown fields or tokens skipped)\n");
    }
    printf("\n");

    for (i = 0u; i < p->field_count; i++) {
        const WhdBoundField *bf = &p->fields[i];
        printf("  [%s]  tlv_id=0x%02X  weight=%u  "
               "include=%u  exclude=%u",
               bf->field_name,
               (unsigned)bf->tlv_field_id,
               (unsigned)bf->weight,
               (unsigned)bf->include_count,
               (unsigned)bf->exclude_count);
        if (bf->has_default) {
            printf("  default_id=%u", (unsigned)bf->default_token_id);
        }
        printf("\n");

        /* Include list */
        for (k = 0u; k < bf->include_count; k++) {
            printf("    include[%u] = 0x%04X  rank=%u\n",
                   (unsigned)k,
                   (unsigned)bf->include_ids[k],
                   (unsigned)k);
        }
        /* Bucket metadata */
        if (bf->bucket_count > 1) {
            uint8_t b;
            printf("    buckets : %u (slash-separated)\n",
                   (unsigned)bf->bucket_count);
            for (b = 0u; b < bf->bucket_count; b++) {
                printf("      bucket[%u] start=%u count=%u\n",
                       (unsigned)b,
                       (unsigned)bf->buckets[b].start,
                       (unsigned)bf->buckets[b].count);
            }
        }
        /* Exclude list */
        for (k = 0u; k < bf->exclude_count; k++) {
            printf("    exclude[%u] = 0x%04X\n",
                   (unsigned)k,
                   (unsigned)bf->exclude_ids[k]);
        }
    }
}

/* End of Text */
