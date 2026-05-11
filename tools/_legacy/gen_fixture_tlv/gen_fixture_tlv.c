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

/* ======================================================================= */
/* Field ID constants                                                       */

#define FID_DISP  0x04u
#define FID_GRPID 0x05u
#define FID_CHIP  0x06u
#define FID_LANG  0x07u
#define FID_MEM   0x08u
#define FID_VIDEO 0x09u
#define FID_MEDIA 0x0Au

/* Field IDs for fallback TLVs (no group_id field; IDs shift down by one) */
#define FB_FID_DISP  0x04u
#define FB_FID_CHIP  0x05u
#define FB_FID_LANG  0x06u
#define FB_FID_MEM   0x07u

/* ======================================================================= */
/* Token ID constants                                                       */
/*
 * These must match tests/filtering/defs/ CSV files:
 *   Chipset:  AGA=1, OCS=2(default), ECS=3, CD32=4
 *   Language: En=1(default), De=2, Fr=3
 *   Memory:   1MB=1(default), 512KB=2
 */

#define AGA      1u
#define OCS      2u
#define ECS      3u
#define CD32     4u
#define EN       1u
#define DE       2u
#define FR       3u
#define MB1      1u
#define KB512    2u
#define PAL_TOK  1u  /* Video: PAL (default) */
#define DISK_TOK 1u  /* Media: Disk (default) */

/* ======================================================================= */
/* Variant emitters                                                         */

/* Variant with group_id (used in TLVs with a group map). */
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

/*
 * Regression variant: same as emit_full_variant but also emits video and media
 * tokens.  Used in build_tiny_regression_games so the TLV field map has 7
 * recognised fields (chipset, language, memory, video, media all with `/`
 * slots in T034).
 */
static void emit_regression_variant(Buf *out, const char *name, uint16_t gid,
                                     uint32_t chip, int has_lang, uint32_t lang,
                                     uint32_t mem)
{
    emit_display_name(out, FID_DISP, name);
    emit_group_id(out, FID_GRPID, gid);
    emit_token(out, FID_CHIP, chip);
    if (has_lang) {
        emit_token(out, FID_LANG, lang);
    }
    emit_token(out, FID_MEM,   mem);
    emit_token(out, FID_VIDEO, PAL_TOK);
    emit_token(out, FID_MEDIA, DISK_TOK);
}

/*
 * Regression nochip variant: like emit_variant_nochip but also emits
 * video and media tokens.
 */
static void emit_regression_nochip(Buf *out, const char *name, uint16_t gid,
                                    int has_lang, uint32_t lang, uint32_t mem)
{
    emit_display_name(out, FID_DISP, name);
    emit_group_id(out, FID_GRPID, gid);
    /* chipset field intentionally absent -- harness uses CSV default (OCS) */
    if (has_lang) {
        emit_token(out, FID_LANG, lang);
    }
    emit_token(out, FID_MEM,   mem);
    emit_token(out, FID_VIDEO, PAL_TOK);
    emit_token(out, FID_MEDIA, DISK_TOK);
}

/* Variant without group_id (fallback TLV, heuristic grouping). */
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
/* build_tiny_games  (legacy fixture -- backward compatibility)             */
/*
 * Still generated to tests/filtering/tiny_games.tlv so the existing
 * T1-T16 batch remains runnable until run_tests.bat is rewritten.
 *
 * 11 variants across 5 groups with group_id field and group map block.
 */

static void build_tiny_games(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "group_id", "chipset",
                                "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u, 0x08u };

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

    emit_full_variant(out, "AlienBreed_v1.0_AGA_En", 1u, AGA, 1, EN, MB1);
    emit_full_variant(out, "AlienBreed_v1.0_OCS_En", 1u, OCS, 1, EN, KB512);
    emit_full_variant(out, "AlienBreed_v1.0_OCS_De", 1u, OCS, 1, DE, KB512);

    emit_full_variant(out, "Banshee_v1.0_AGA_En", 2u, AGA, 1, EN, MB1);
    emit_full_variant(out, "Banshee_v1.0_AGA_De", 2u, AGA, 1, DE, MB1);

    emit_full_variant(out, "CannonFodder_v1.0_AGA_En", 3u, AGA, 1, EN, MB1);
    emit_full_variant(out, "CannonFodder_v1.0_AGA_De", 3u, AGA, 1, DE, MB1);

    emit_full_variant(out, "DynaBlaster_v1.0a_AGA_En", 4u, AGA, 1, EN, MB1);
    emit_full_variant(out, "DynaBlaster_v1.0b_AGA_En", 4u, AGA, 1, EN, MB1);

    emit_full_variant(out, "EaglesRider_v1.0_AGA", 5u, AGA, 0, 0u, MB1);
    emit_full_variant(out, "EaglesRider_v1.0_OCS", 5u, OCS, 0, 0u, KB512);
}

/* ======================================================================= */
/* build_tiny_games_fallback  (legacy fixture -- backward compatibility)    */
/*
 * Still generated to tests/filtering/tiny_games_fallback.tlv.
 * No group_id field, no group map block; grouping uses heuristic.
 */

static void build_tiny_games_fallback(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "chipset", "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u };

    emit_field_map(out, fnames, fids, 4);
    emit_crc_block(out, crcs, ncrc);

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
/* build_tiny_regression_games                                              */
/*
 * Main fixture for T001-T027, T035, T042.
 * 22 groups, 44 variants, full group map block, group_id field.
 *
 * All TLV entry orderings are chosen deliberately:
 *   - Group 1  AlienBreed:     OCS first, then ECS, then AGA (T001 proves AGA wins)
 *   - Group 2  AlienBreed2:    De first, then En, then Fr    (T002 proves Fr wins)
 *   - Group 3  GameA:          AGA_De first, OCS_En second   (T003/T004 weight tests)
 *   - Group 9  TieGame:        v1.0 first (T010 expects v1.0 to win equal-score tie)
 *   - Group 10 TieGame2:       v1.1 first (T011 expects v1.1 to win equal-score tie)
 *   - Group 12 Lotus:          OCS first, AGA second (T020 expects AGA after scoring)
 *
 * Token IDs:
 *   Chipset:  AGA=1, OCS=2, ECS=3, CD32=4   (default = OCS via CSV)
 *   Language: En=1, De=2, Fr=3               (default = En  via CSV)
 *   Memory:   1MB=1, 512KB=2                 (default = 1MB via CSV)
 *
 * All group_id values are written big-endian (T040/T042 endian coverage).
 */

static void build_tiny_regression_games(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "group_id", "chipset",
                                "language", "memory", "video", "media" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au };

    const GroupEntry groups[] = {
        {  1u, "AlienBreed"          },  /* T001, T012 */
        {  2u, "AlienBreed2"         },  /* T002, T012 */
        {  3u, "GameA"               },  /* T003, T004 */
        {  4u, "Banshee"             },  /* T005       */
        {  5u, "CD32OnlyGame"        },  /* T006       */
        {  6u, "GameB"               },  /* T007       */
        {  7u, "DefaultChipGame"     },  /* T008       */
        {  8u, "DefaultExcluded"     },  /* T009       */
        {  9u, "TieGame"             },  /* T010       */
        { 10u, "TieGame2"            },  /* T011       */
        { 11u, "WeirdNameNoVersion"  },  /* T014       */
        { 12u, "Lotus"               },  /* T015-T020  */
        { 13u, "Lotus2"              },  /* T015-T019  */
        { 14u, "Lotus3"              },  /* T015-T019  */
        { 15u, "LotusTurbo"          },  /* T015, T017 */
        { 16u, "BucketGame"          },  /* T021, T022 */
        { 17u, "BucketMissing"       },  /* T023       */
        { 18u, "BucketExclude"       },  /* T024       */
        { 19u, "BucketDup"           },  /* T025       */
        { 20u, "CartGame"            },  /* T026       */
        { 21u, "CartMissing"         },  /* T027       */
        { 22u, "EndianTokenGame"     }   /* T042       */
    };

    emit_field_map(out, fnames, fids, 7);
    emit_group_map(out, groups, 22);
    emit_crc_block(out, crcs, ncrc);

    /* --- Group 1: AlienBreed (T001 - AGA beats ECS beats OCS) --- */
    emit_regression_variant(out, "AlienBreed_v1.0_OCS_En",  1u, OCS, 1, EN, KB512);
    emit_regression_variant(out, "AlienBreed_v1.0_ECS_En",  1u, ECS, 1, EN, KB512);
    emit_regression_variant(out, "AlienBreed_v1.0_AGA_En",  1u, AGA, 1, EN, MB1);

    /* --- Group 2: AlienBreed2 (T002 - language Fr beats En beats De) --- */
    emit_regression_variant(out, "AlienBreed2_v1.0_AGA_De", 2u, AGA, 1, DE, MB1);
    emit_regression_variant(out, "AlienBreed2_v1.0_AGA_En", 2u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "AlienBreed2_v1.0_AGA_Fr", 2u, AGA, 1, FR, MB1);

    /* --- Group 3: GameA (T003/T004 - weight determines whether AGA or En wins) --- */
    emit_regression_variant(out, "GameA_v1.0_AGA_De",        3u, AGA, 1, DE, MB1);
    emit_regression_variant(out, "GameA_v1.0_OCS_En",        3u, OCS, 1, EN, KB512);

    /* --- Group 4: Banshee (T005 - exclude beats high score) --- */
    emit_regression_variant(out, "Banshee_v1.0_AGA_En",      4u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "Banshee_v1.0_OCS_En",      4u, OCS, 1, EN, KB512);

    /* --- Group 5: CD32OnlyGame (T006 - excluded-only group -> no output) --- */
    emit_regression_variant(out, "CD32OnlyGame_v1.0_CD32_En", 5u, CD32, 1, EN, MB1);

    /* --- Group 6: GameB (T007 - exclude with zero weight) --- */
    emit_regression_variant(out, "GameB_v1.0_AGA_En",        6u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "GameB_v1.0_OCS_En",        6u, OCS, 1, EN, KB512);

    /* --- Group 7: DefaultChipGame (T008 - missing chipset uses CSV default OCS) --- */
    /* First variant has no chipset field; harness must infer OCS as default.      */
    emit_regression_nochip(out, "DefaultChipGame_v1.0_En",     7u, 1, EN, MB1);
    emit_regression_variant(out, "DefaultChipGame_v1.0_AGA_En", 7u, AGA, 1, EN, MB1);

    /* --- Group 8: DefaultExcluded (T009 - CSV default OCS can be excluded) --- */
    emit_regression_nochip(out, "DefaultExcluded_v1.0_En",     8u, 1, EN, MB1);
    emit_regression_variant(out, "DefaultExcluded_v1.0_AGA_En", 8u, AGA, 1, EN, MB1);

    /* --- Group 9: TieGame (T010 - equal scores; v1.0 is first in TLV -> wins) --- */
    emit_regression_variant(out, "TieGame_v1.0_AGA_En",     9u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "TieGame_v1.1_AGA_En",     9u, AGA, 1, EN, MB1);

    /* --- Group 10: TieGame2 (T011 - equal scores; v1.1 is first in TLV -> wins) --- */
    emit_regression_variant(out, "TieGame2_v1.1_AGA_En",   10u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "TieGame2_v1.0_AGA_En",   10u, AGA, 1, EN, MB1);

    /* --- Group 11: WeirdNameNoVersion (T014 - no _v<digit> pattern in name) --- */
    emit_regression_variant(out, "WeirdNameNoVersion_AGA_En", 11u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "WeirdNameNoVersion_OCS_En", 11u, OCS, 1, EN, KB512);

    /* --- Group 12: Lotus (T015-T020 - search + scoring) --- */
    /* OCS placed first; AGA must win under chipset-priority profile (T020). */
    emit_regression_variant(out, "Lotus_v1.0_OCS_En",      12u, OCS, 1, EN, KB512);
    emit_regression_variant(out, "Lotus_v1.0_AGA_En",      12u, AGA, 1, EN, MB1);

    /* --- Group 13: Lotus2 (T015-T019) --- */
    emit_regression_variant(out, "Lotus2_v1.0_AGA_En",     13u, AGA, 1, EN, MB1);

    /* --- Group 14: Lotus3 (T015-T019) --- */
    emit_regression_variant(out, "Lotus3_v1.0_AGA_En",     14u, AGA, 1, EN, MB1);

    /* --- Group 15: LotusTurbo (T015, T017) --- */
    /* T017 uses --search Lotus? which must match Lotus2/Lotus3 but NOT LotusTurbo. */
    emit_regression_variant(out, "LotusTurbo_v1.0_AGA_En", 15u, AGA, 1, EN, MB1);

    /* --- Group 16: BucketGame (T021, T022 - slash bucket chipset lanes) --- */
    emit_regression_variant(out, "BucketGame_v1.0_AGA_En", 16u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "BucketGame_v1.0_ECS_En", 16u, ECS, 1, EN, KB512);
    emit_regression_variant(out, "BucketGame_v1.0_OCS_En", 16u, OCS, 1, EN, KB512);

    /* --- Group 17: BucketMissing (T023 - OCS lane has no eligible variant) --- */
    emit_regression_variant(out, "BucketMissing_v1.0_AGA_En", 17u, AGA, 1, EN, MB1);

    /* --- Group 18: BucketExclude (T024 - global exclude removes OCS from all lanes) --- */
    emit_regression_variant(out, "BucketExclude_v1.0_AGA_En", 18u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "BucketExclude_v1.0_OCS_En", 18u, OCS, 1, EN, KB512);

    /* --- Group 19: BucketDup (T025 - AGA selected by lane 0; lane 1 must use OCS) --- */
    emit_regression_variant(out, "BucketDup_v1.0_AGA_En",  19u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "BucketDup_v1.0_OCS_En",  19u, OCS, 1, EN, KB512);

    /* --- Group 20: CartGame (T026 - Cartesian product: 2 chipsets x 2 languages) --- */
    emit_regression_variant(out, "CartGame_v1.0_AGA_En",   20u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "CartGame_v1.0_AGA_De",   20u, AGA, 1, DE, MB1);
    emit_regression_variant(out, "CartGame_v1.0_OCS_En",   20u, OCS, 1, EN, KB512);
    emit_regression_variant(out, "CartGame_v1.0_OCS_De",   20u, OCS, 1, DE, KB512);

    /* --- Group 21: CartMissing (T027 - Cartesian product with missing combos) --- */
    emit_regression_variant(out, "CartMissing_v1.0_AGA_En", 21u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "CartMissing_v1.0_OCS_De", 21u, OCS, 1, DE, KB512);

    /* --- Group 22: EndianTokenGame (T042 - token IDs must be read little-endian) --- */
    emit_regression_variant(out, "EndianTokenGame_v1.0_AGA_En", 22u, AGA, 1, EN, MB1);
    emit_regression_variant(out, "EndianTokenGame_v1.0_OCS_De", 22u, OCS, 1, DE, KB512);
}

/* ======================================================================= */
/* build_tiny_legacy_no_group_map                                           */
/*
 * T013: fallback grouping via derive_group_name heuristic.
 * No group_id field in the field map, no group map block.
 * The harness must derive group "FallbackGame" from both display names.
 * AGA variant placed second so AGA wins under chipset priority profile.
 */

static void build_tiny_legacy_no_group_map(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "chipset", "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u };

    emit_field_map(out, fnames, fids, 4);
    /* No group map block (block 0x02) -- grouping is purely by heuristic */
    emit_crc_block(out, crcs, ncrc);

    emit_fallback_variant(out, "FallbackGame_v1.0_OCS_En", OCS, 1, EN, KB512);
    emit_fallback_variant(out, "FallbackGame_v1.1_AGA_En", AGA, 1, EN, MB1);
}

/* ======================================================================= */
/* build_tiny_groupid_high                                                  */
/*
 * T040: group_id = 300 (0x012C) tests big-endian uint16 read.
 * If the harness reads the field as little-endian it gets 0x2C01 = 11265
 * and the two variants fail to group together.  Only correct BE decoding
 * puts both variants into group 300 → AGA wins under chipset priority.
 */

static void build_tiny_groupid_high(Buf *out, const CrcEntry *crcs, int ncrc)
{
    const char   *fnames[] = { "display_name", "group_id", "chipset",
                                "language", "memory" };
    const uint8_t fids[]   = { 0x04u, 0x05u, 0x06u, 0x07u, 0x08u };
    GroupEntry groups[1];

    groups[0].id   = 300u; /* 0x012C -- needs big-endian write and read */
    groups[0].name = "GroupHigh";

    emit_field_map(out, fnames, fids, 5);
    emit_group_map(out, groups, 1);
    emit_crc_block(out, crcs, ncrc);

    emit_full_variant(out, "GroupHigh_v1.0_AGA_En", 300u, AGA, 1, EN, MB1);
    emit_full_variant(out, "GroupHigh_v1.0_OCS_En", 300u, OCS, 1, EN, KB512);
}

/* ======================================================================= */
/* build_tiny_crc_mismatch                                                  */
/*
 * T036/T037: structurally identical to tiny_regression_games but every
 * embedded CRC value is XOR-inverted so none can match the live defs.
 * Strict mode (T036) must reject; warn-only mode (T037) must continue.
 */

static void build_tiny_crc_mismatch(Buf *out, const CrcEntry *crcs, int ncrc)
{
    CrcEntry bad_crcs[5];
    int i;
    for (i = 0; i < ncrc && i < 5; i++) {
        bad_crcs[i].csv_name = crcs[i].csv_name;
        bad_crcs[i].crc      = crcs[i].crc ^ 0xFFFFFFFFUL;
    }
    build_tiny_regression_games(out, bad_crcs, ncrc);
}

/* ======================================================================= */
/* build_tiny_bad_no_fieldmap                                               */
/*
 * T038: starts with a CRC block (type 0x04) instead of the expected field
 * map block (type 0x01).  The harness must reject the file and report that
 * the field map is missing or invalid.
 */

static void build_tiny_bad_no_fieldmap(Buf *out)
{
    /* CRC block with zero entries as the very first block.
     * Block 0x01 (field map) is entirely absent.              */
    buf_u8(out, 0x04u);   /* block type: CRC fingerprints */
    buf_u16le(out, 2u);   /* payload length: 2 bytes      */
    buf_u16le(out, 0u);   /* count = 0 entries            */
}

/* ======================================================================= */
/* main                                                                     */
/*
 * Usage: gen_fixture_tlv [--out-dir <dir>] [--defs <dir>]
 *
 * --out-dir  Directory to write new test fixtures (default: tests/filtering/tlv)
 * --defs     Directory containing test CSV files  (default: tests/filtering/defs)
 *
 * Legacy tiny_games.tlv and tiny_games_fallback.tlv are always written to
 * tests/filtering/ for backward compatibility with any remaining T1-T16 runs.
 */

int main(int argc, char *argv[])
{
    const char *defs_dir = "tests/filtering/defs";
    const char *out_dir  = "tests/filtering/tlv";
    char chip_path[512], lang_path[512], mem_path[512];
    char vid_path[512],  med_path[512];
    char out_path[512];
    CrcEntry crcs[5];
    Buf tlv;
    int i;

    /* Simple argument parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--defs") == 0 && i + 1 < argc) {
            defs_dir = argv[++i];
        }
    }

    snprintf(chip_path, sizeof(chip_path), "%s/Chipset.csv",  defs_dir);
    snprintf(lang_path, sizeof(lang_path), "%s/Language.csv", defs_dir);
    snprintf(mem_path,  sizeof(mem_path),  "%s/Memory.csv",   defs_dir);
    snprintf(vid_path,  sizeof(vid_path),  "%s/Video.csv",    defs_dir);
    snprintf(med_path,  sizeof(med_path),  "%s/Media.csv",    defs_dir);

    /* Compute CRCs in text mode -- identical to tlv_crc_validate.c */
    crcs[0].csv_name = "Chipset";
    crcs[0].crc      = crc_of_file(chip_path);
    crcs[1].csv_name = "Language";
    crcs[1].crc      = crc_of_file(lang_path);
    crcs[2].csv_name = "Memory";
    crcs[2].crc      = crc_of_file(mem_path);
    crcs[3].csv_name = "Video";
    crcs[3].crc      = crc_of_file(vid_path);
    crcs[4].csv_name = "Media";
    crcs[4].crc      = crc_of_file(med_path);

    printf("CRC Chipset  = 0x%08X\n", crcs[0].crc);
    printf("CRC Language = 0x%08X\n", crcs[1].crc);
    printf("CRC Memory   = 0x%08X\n", crcs[2].crc);
    printf("CRC Video    = 0x%08X\n", crcs[3].crc);
    printf("CRC Media    = 0x%08X\n", crcs[4].crc);
    printf("Output dir   : %s\n\n",   out_dir);

    /* ------------------------------------------------------------------ */
    /* Legacy fixtures (backward compatibility with existing T1-T16 tests) */
    /* ------------------------------------------------------------------ */

    buf_init(&tlv);
    build_tiny_games(&tlv, crcs, 3);
    buf_save(&tlv, "tests/filtering/tiny_games.tlv");
    printf("Written: tests/filtering/tiny_games.tlv (%u bytes)\n",
           (unsigned)tlv.len);
    buf_free(&tlv);

    buf_init(&tlv);
    build_tiny_games_fallback(&tlv, crcs, 3);
    buf_save(&tlv, "tests/filtering/tiny_games_fallback.tlv");
    printf("Written: tests/filtering/tiny_games_fallback.tlv (%u bytes)\n",
           (unsigned)tlv.len);
    buf_free(&tlv);

    /* ------------------------------------------------------------------ */
    /* New T001-T045 fixtures written to out_dir                           */
    /* ------------------------------------------------------------------ */

    /* T001-T027, T035, T042: main 22-group regression fixture */
    snprintf(out_path, sizeof(out_path), "%s/tiny_regression_games.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_regression_games(&tlv, crcs, 5);
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    /* T013: heuristic fallback grouping (FallbackGame, no group_id) */
    snprintf(out_path, sizeof(out_path), "%s/tiny_legacy_no_group_map.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_legacy_no_group_map(&tlv, crcs, 3);
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    /* T040: group_id = 300 (0x012C) big-endian correctness */
    snprintf(out_path, sizeof(out_path), "%s/tiny_groupid_high.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_groupid_high(&tlv, crcs, 3);
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    /* T036/T037: CRC mismatch (all embedded CRCs are XOR-inverted) */
    snprintf(out_path, sizeof(out_path), "%s/tiny_crc_mismatch_base.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_crc_mismatch(&tlv, crcs, 5);
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    /* T038: missing field map (first block is 0x04, not 0x01) */
    snprintf(out_path, sizeof(out_path), "%s/tiny_bad_no_fieldmap.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_bad_no_fieldmap(&tlv);
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    /* T039: truncated TLV -- cut at byte 50 to land inside the field map block,
     * guaranteeing invalid structure (not just a missing trailing record).    */
    snprintf(out_path, sizeof(out_path), "%s/tiny_bad_truncated.tlv", out_dir);
    buf_init(&tlv);
    build_tiny_regression_games(&tlv, crcs, 5);
    if (tlv.len > 50u) {
        tlv.len = 50u;  /* truncate inside field map block; buf_save uses tlv.len */
    }
    buf_save(&tlv, out_path);
    printf("Written: %s (%u bytes, truncated)\n", out_path, (unsigned)tlv.len);
    buf_free(&tlv);

    return 0;
}

/* End of Text */

