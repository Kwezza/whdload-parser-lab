/* tools/gen_fixture_tlv/gen_fixture_tlv.c
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Stage J regression fixture generator.
 *
 * Generates two small TLV files used by the filtering harness tests:
 *
 *   tests/filtering/tiny_games.tlv
 *       Has group_id field (0x05) and group map block (0x02).
 *       Tests: group_id path, tie case, default-token case,
 *              all-rejected group, CRC validation.
 *
 *   tests/filtering/tiny_games_fallback.tlv
 *       No group_id field, no group map block.
 *       Tests: display_name heuristic fallback grouping.
 *
 * Both TLVs embed CRC fingerprints for the three test CSV files in
 * tests/filtering/defs/ using the same text-mode CRC method as the
 * production validator (fgets in "r" mode so \r\n -> \n on Windows).
 *
 * Variant data (11 variants across 5 groups):
 *
 *   Group 1 (AlienBreed, id=1):
 *     AlienBreed_v1.0_AGA_En   chipset=AGA(1)  lang=En(1)  mem=1MB(1)
 *     AlienBreed_v1.0_OCS_En   chipset=OCS(2)  lang=En(1)  mem=512KB(2)
 *     AlienBreed_v1.0_OCS_De   chipset=OCS(2)  lang=De(2)  mem=512KB(2)
 *
 *   Group 2 (Banshee, id=2):
 *     Banshee_v1.0_AGA_En      chipset=AGA(1)  lang=En(1)  mem=1MB(1)
 *     Banshee_v1.0_AGA_De      chipset=AGA(1)  lang=De(2)  mem=1MB(1)
 *
 *   Group 3 (CannonFodder, id=3):
 *     CannonFodder_v1.0_AGA_En chipset=AGA(1)  lang=En(1)  mem=1MB(1)
 *     CannonFodder_v1.0_AGA_De chipset=AGA(1)  lang=De(2)  mem=1MB(1)
 *
 *   Group 4 (DynaBlaster, id=4) -- TIE CASE:
 *     DynaBlaster_v1.0a_AGA_En chipset=AGA(1)  lang=En(1)  mem=1MB(1)
 *     DynaBlaster_v1.0b_AGA_En chipset=AGA(1)  lang=En(1)  mem=1MB(1)
 *     (identical scores under both profiles; first variant wins)
 *
 *   Group 5 (EaglesRider, id=5) -- DEFAULT TOKEN CASE:
 *     EaglesRider_v1.0_AGA     chipset=AGA(1)  (no lang field)  mem=1MB(1)
 *     EaglesRider_v1.0_OCS     chipset=OCS(2)  (no lang field)  mem=512KB(2)
 *     (missing language field falls back to CSV default: En=1)
 *
 * Field IDs (tiny_games.tlv):
 *   0x04  display_name  (variant boundary marker)
 *   0x05  group_id      (2-byte big-endian uint16; structural, not scored)
 *   0x06  chipset       (4-byte LE uint32 token ID)
 *   0x07  language      (4-byte LE uint32 token ID)
 *   0x08  memory        (4-byte LE uint32 token ID)
 *
 * Field IDs (tiny_games_fallback.tlv):
 *   0x04  display_name
 *   0x05  chipset
 *   0x06  language
 *   0x07  memory
 *   (group_id absent; grouping uses derive_group_name heuristic)
 *
 * C99 (host-only tool; not required to be C89-compatible).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ======================================================================= */
/* Inline CRC-32/ISO-HDLC (same polynomial as src/utils/crc32.c)          */

static uint32_t s_crc_table[256];
static int      s_crc_ready = 0;

static void crc_build(void)
{
    uint32_t i, j, v;
    for (i = 0; i < 256; i++) {
        v = i;
        for (j = 0; j < 8; j++) {
            v = (v & 1u) ? ((v >> 1) ^ 0xEDB88320UL) : (v >> 1);
        }
        s_crc_table[i] = v;
    }
    s_crc_ready = 1;
}

static uint32_t crc_update(uint32_t crc, const unsigned char *data, size_t len)
{
    size_t i;
    if (!s_crc_ready) crc_build();
    for (i = 0; i < len; i++) {
        crc = s_crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static uint32_t crc_of_file(const char *path)
{
    FILE    *f;
    char     line[4096];
    uint32_t crc = 0xFFFFFFFFUL;

    f = fopen(path, "r"); /* text mode -- same as the validator */
    if (!f) {
        fprintf(stderr, "gen_fixture_tlv: cannot open %s\n", path);
        exit(1);
    }
    while (fgets(line, (int)sizeof(line), f)) {
        crc = crc_update(crc, (const unsigned char *)line, strlen(line));
    }
    fclose(f);
    return crc ^ 0xFFFFFFFFUL;
}

/* ======================================================================= */
/* Byte buffer helper                                                       */

typedef struct Buf {
    unsigned char *data;
    size_t         len;
    size_t         cap;
} Buf;

static void buf_init(Buf *b)
{
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void buf_push(Buf *b, const unsigned char *src, size_t n)
{
    if (b->len + n > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 1024;
        while (new_cap < b->len + n) new_cap *= 2;
        b->data = (unsigned char *)realloc(b->data, new_cap);
        if (!b->data) { fprintf(stderr, "OOM\n"); exit(1); }
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_u8(Buf *b, uint8_t v)
{
    buf_push(b, &v, 1);
}

static void buf_u16le(Buf *b, uint16_t v)
{
    unsigned char tmp[2];
    tmp[0] = (unsigned char)(v & 0xFFu);
    tmp[1] = (unsigned char)(v >> 8);
    buf_push(b, tmp, 2);
}

static void buf_u16be(Buf *b, uint16_t v)
{
    unsigned char tmp[2];
    tmp[0] = (unsigned char)(v >> 8);
    tmp[1] = (unsigned char)(v & 0xFFu);
    buf_push(b, tmp, 2);
}

static void buf_u32le(Buf *b, uint32_t v)
{
    unsigned char tmp[4];
    tmp[0] = (unsigned char)( v        & 0xFFu);
    tmp[1] = (unsigned char)((v >>  8) & 0xFFu);
    tmp[2] = (unsigned char)((v >> 16) & 0xFFu);
    tmp[3] = (unsigned char)((v >> 24) & 0xFFu);
    buf_push(b, tmp, 4);
}

static void buf_str(Buf *b, const char *s)  /* without NUL */
{
    buf_push(b, (const unsigned char *)s, strlen(s));
}

static void buf_cstr(Buf *b, const char *s) /* with NUL */
{
    buf_push(b, (const unsigned char *)s, strlen(s) + 1);
}

static void buf_save(const Buf *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gen_fixture_tlv: cannot write %s\n", path); exit(1); }
    fwrite(b->data, 1, b->len, f);
    fclose(f);
}

/* ======================================================================= */
/* Field-map block helper                                                   */

static void emit_field_map(Buf *out, const char **names, const uint8_t *ids, int n)
{
    Buf payload;
    int i;

    buf_init(&payload);
    for (i = 0; i < n; i++) {
        buf_u8(&payload, ids[i]);
        buf_cstr(&payload, names[i]);
    }

    buf_u8(out, 0x01u);                        /* block type              */
    buf_u16le(out, (uint16_t)payload.len);     /* 2-byte LE payload size  */
    buf_push(out, payload.data, payload.len);
    buf_free(&payload);
}

/* ======================================================================= */
/* Group-map block helper                                                   */

typedef struct GroupEntry {
    uint16_t    id;
    const char *name;
} GroupEntry;

static void emit_group_map(Buf *out, const GroupEntry *groups, int n)
{
    Buf payload;
    int i;

    buf_init(&payload);
    buf_u16le(&payload, (uint16_t)n);          /* group count LE          */
    for (i = 0; i < n; i++) {
        size_t nlen = strlen(groups[i].name);
        buf_u16be(&payload, groups[i].id);     /* group_id  BE            */
        buf_u8(&payload, (uint8_t)nlen);       /* name length             */
        buf_str(&payload, groups[i].name);     /* name bytes (no NUL)     */
    }

    buf_u8(out, 0x02u);
    buf_u16le(out, (uint16_t)payload.len);
    buf_push(out, payload.data, payload.len);
    buf_free(&payload);
}

/* ======================================================================= */
/* CRC-fingerprint block helper                                             */

typedef struct CrcEntry {
    const char *csv_name;  /* base name, no .csv extension */
    uint32_t    crc;
} CrcEntry;

static void emit_crc_block(Buf *out, const CrcEntry *entries, int n)
{
    Buf payload;
    int i;

    buf_init(&payload);
    buf_u16le(&payload, (uint16_t)n);          /* count LE                */
    for (i = 0; i < n; i++) {
        buf_cstr(&payload, entries[i].csv_name); /* NUL-terminated name   */
        buf_u32le(&payload, entries[i].crc);     /* 4-byte LE CRC         */
    }

    buf_u8(out, 0x04u);
    buf_u16le(out, (uint16_t)payload.len);
    buf_push(out, payload.data, payload.len);
    buf_free(&payload);
}

/* ======================================================================= */
/* Data-record emitters                                                     */

/* Write a display_name record (the variant boundary marker).             */
static void emit_display_name(Buf *out, uint8_t disp_id, const char *name)
{
    size_t len = strlen(name);
    buf_u8(out, disp_id);
    buf_u16le(out, (uint16_t)len);
    buf_str(out, name);
}

/* Write a group_id record (2-byte BE uint16 value).                      */
static void emit_group_id(Buf *out, uint8_t gid_field, uint16_t gid)
{
    buf_u8(out, gid_field);
    buf_u16le(out, 2u);
    buf_u16be(out, gid);
}

/* Write a 4-byte LE token-ID field record.                               */
static void emit_token(Buf *out, uint8_t field_id, uint32_t token_id)
{
    buf_u8(out, field_id);
    buf_u16le(out, 4u);
    buf_u32le(out, token_id);
}

/* ======================================================================= */
/* Variant emitters for tiny_games.tlv (with group_id at 0x05)            */
/*
 * Token IDs (matches tests/filtering/defs/ CSV files):
 *   Chipset:  AGA=1, OCS=2 (default=OCS)
 *   Language: En=1 (default), De=2
 *   Memory:   1MB=1 (default), 512KB=2
 */

#define FID_DISP  0x04u
#define FID_GRPID 0x05u
#define FID_CHIP  0x06u
#define FID_LANG  0x07u
#define FID_MEM   0x08u

#define AGA   1u
#define OCS   2u
#define EN    1u
#define DE    2u
#define MB1   1u
#define KB512 2u

static void emit_full_variant(Buf *out, const char *name, uint16_t gid,
                               uint32_t chip, int has_lang, uint32_t lang,
                               uint32_t mem)
{
    emit_display_name(out, FID_DISP, name);
    emit_group_id(out, FID_GRPID, gid);
    emit_token(out, FID_CHIP, chip);
    if (has_lang) {
        emit_token(out, FID_LANG, lang);
    }
    emit_token(out, FID_MEM, mem);
}

/* ======================================================================= */
/* Variant emitters for tiny_games_fallback.tlv (no group_id field)       */
/*
 * Field IDs shifted: display_name=0x04, chipset=0x05, language=0x06, memory=0x07
 */

#define FB_FID_DISP  0x04u
#define FB_FID_CHIP  0x05u
#define FB_FID_LANG  0x06u
#define FB_FID_MEM   0x07u

static void emit_fallback_variant(Buf *out, const char *name,
                                   uint32_t chip, int has_lang, uint32_t lang,
                                   uint32_t mem)
{
    emit_display_name(out, FB_FID_DISP, name);
    emit_token(out, FB_FID_CHIP, chip);
    if (has_lang) {
        emit_token(out, FB_FID_LANG, lang);
    }
    emit_token(out, FB_FID_MEM, mem);
}

/* ======================================================================= */
/* Build tiny_games.tlv                                                     */

static void build_tiny_games(Buf *out, const CrcEntry *crcs, int ncrc)
{
    /* Field map */
    const char   *fnames[] = { "display_name", "group_id", "chipset",
                                "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u, 0x08u };

    /* Group map */
    const GroupEntry groups[] = {
        { 1u, "AlienBreed"   },
        { 2u, "Banshee"      },
        { 3u, "CannonFodder" },
        { 4u, "DynaBlaster"  },
        { 5u, "EaglesRider"  }
    };

    emit_field_map(out, fnames, fids, 5);
    emit_group_map(out, groups, 5);
    emit_crc_block(out, crcs, ncrc);

    /* --- Group 1: AlienBreed --- */
    emit_full_variant(out, "AlienBreed_v1.0_AGA_En", 1u, AGA, 1, EN, MB1);
    emit_full_variant(out, "AlienBreed_v1.0_OCS_En", 1u, OCS, 1, EN, KB512);
    emit_full_variant(out, "AlienBreed_v1.0_OCS_De", 1u, OCS, 1, DE, KB512);

    /* --- Group 2: Banshee --- */
    emit_full_variant(out, "Banshee_v1.0_AGA_En", 2u, AGA, 1, EN, MB1);
    emit_full_variant(out, "Banshee_v1.0_AGA_De", 2u, AGA, 1, DE, MB1);

    /* --- Group 3: CannonFodder (all AGA; all-rejected with ocs_only profile) --- */
    emit_full_variant(out, "CannonFodder_v1.0_AGA_En", 3u, AGA, 1, EN, MB1);
    emit_full_variant(out, "CannonFodder_v1.0_AGA_De", 3u, AGA, 1, DE, MB1);

    /* --- Group 4: DynaBlaster (tie case; identical scores; first wins) --- */
    emit_full_variant(out, "DynaBlaster_v1.0a_AGA_En", 4u, AGA, 1, EN, MB1);
    emit_full_variant(out, "DynaBlaster_v1.0b_AGA_En", 4u, AGA, 1, EN, MB1);

    /* --- Group 5: EaglesRider (default-token case; no language field) --- */
    emit_full_variant(out, "EaglesRider_v1.0_AGA", 5u, AGA, 0, 0u, MB1);
    emit_full_variant(out, "EaglesRider_v1.0_OCS", 5u, OCS, 0, 0u, KB512);
}

/* ======================================================================= */
/* Build tiny_games_fallback.tlv (no group_id, no group map)               */

static void build_tiny_games_fallback(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "chipset", "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u };

    emit_field_map(out, fnames, fids, 4);
    /* no group map block */
    emit_crc_block(out, crcs, ncrc);

    /* Same variant data, same order; grouping is by heuristic */
    emit_fallback_variant(out, "AlienBreed_v1.0_AGA_En", AGA, 1, EN, MB1);
    emit_fallback_variant(out, "AlienBreed_v1.0_OCS_En", OCS, 1, EN, KB512);
    emit_fallback_variant(out, "AlienBreed_v1.0_OCS_De", OCS, 1, DE, KB512);

    emit_fallback_variant(out, "Banshee_v1.0_AGA_En", AGA, 1, EN, MB1);
    emit_fallback_variant(out, "Banshee_v1.0_AGA_De", AGA, 1, DE, MB1);

    emit_fallback_variant(out, "CannonFodder_v1.0_AGA_En", AGA, 1, EN, MB1);
    emit_fallback_variant(out, "CannonFodder_v1.0_AGA_De", AGA, 1, DE, MB1);

    emit_fallback_variant(out, "DynaBlaster_v1.0a_AGA_En", AGA, 1, EN, MB1);
    emit_fallback_variant(out, "DynaBlaster_v1.0b_AGA_En", AGA, 1, EN, MB1);

    emit_fallback_variant(out, "EaglesRider_v1.0_AGA", AGA, 0, 0u, MB1);
    emit_fallback_variant(out, "EaglesRider_v1.0_OCS", OCS, 0, 0u, KB512);
}

/* ======================================================================= */
/* main                                                                     */

int main(void)
{
    const char *defs_dir = "tests/filtering/defs";
    char  chip_path[512], lang_path[512], mem_path[512];
    CrcEntry crcs[3];
    Buf tlv;

    snprintf(chip_path, sizeof(chip_path), "%s/Chipset.csv",  defs_dir);
    snprintf(lang_path, sizeof(lang_path), "%s/Language.csv", defs_dir);
    snprintf(mem_path,  sizeof(mem_path),  "%s/Memory.csv",   defs_dir);

    /* Compute CRCs (text mode, identical to tlv_crc_validate.c) */
    crcs[0].csv_name = "Chipset";
    crcs[0].crc      = crc_of_file(chip_path);
    crcs[1].csv_name = "Language";
    crcs[1].crc      = crc_of_file(lang_path);
    crcs[2].csv_name = "Memory";
    crcs[2].crc      = crc_of_file(mem_path);

    printf("CRC Chipset  = 0x%08X\n", crcs[0].crc);
    printf("CRC Language = 0x%08X\n", crcs[1].crc);
    printf("CRC Memory   = 0x%08X\n", crcs[2].crc);

    /* --- Write tiny_games.tlv --- */
    buf_init(&tlv);
    build_tiny_games(&tlv, crcs, 3);
    buf_save(&tlv, "tests/filtering/tiny_games.tlv");
    printf("Written: tests/filtering/tiny_games.tlv (%u bytes)\n",
           (unsigned)tlv.len);
    buf_free(&tlv);

    /* --- Write tiny_games_fallback.tlv --- */
    buf_init(&tlv);
    build_tiny_games_fallback(&tlv, crcs, 3);
    buf_save(&tlv, "tests/filtering/tiny_games_fallback.tlv");
    printf("Written: tests/filtering/tiny_games_fallback.tlv (%u bytes)\n",
           (unsigned)tlv.len);
    buf_free(&tlv);

    return 0;
}

/* End of Text */
