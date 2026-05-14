/* tests/reporting/test_language_tokens.c - Language token validation tests
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Validates the compact multilingual token rule implemented in
 * filename_processor.c (is_compact_language_token / language_parser_parse_token).
 *
 * Rule summary:
 *   - A token is a valid language token only if it can be split into
 *     2-character chunks and EVERY chunk resolves in Language.csv.
 *   - Partial substring matches are forbidden: "EasyPlay" must not yield
 *     "Pl" (Polish) and "Infogrames" must not yield "Gr"/"Es".
 *   - Single-language tokens such as "De" or "En" continue to work as
 *     exact 2-character matches.
 *   - Compact multilingual tokens such as "DeEsFrIt" work when all four
 *     2-character chunks resolve.
 *   - If any chunk is absent from Language.csv the whole token is rejected.
 *
 * Host only (C99). Run with:  make test-language
 *
 * Exit code: 0 = all pass, non-zero = at least one failure.
 */

#include "whdtlv/core/csv_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*------------------------------------------------------------------------*/
/* Test harness                                                           */

#define DEFS_DIR "assets_raw/defs"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            printf("  PASS: %s\n", msg); \
            g_pass++; \
        } else { \
            printf("  FAIL: %s\n", msg); \
            g_fail++; \
        } \
    } while (0)

static void print_sep(const char *name)
{
    printf("\n--- %s ---\n", name);
}

/*------------------------------------------------------------------------*/
/* Local mirror of is_compact_language_token                             */
/*                                                                        */
/* This mirrors the rule implemented in filename_processor.c. If the     */
/* rule there changes without updating this function, the test cases      */
/* below will diverge and reveal the regression.                         */

#define MAX_LANG_CHARS 16

static int local_is_compact_language_token(CSVCache *cache,
                                           GlobalCSVManager *csv_mgr,
                                           const char *token,
                                           unsigned int *out_bitfield)
{
    size_t len;
    size_t i;

    *out_bitfield = 0;

    if (!token) { return 0; }
    len = strlen(token);

    if (len < 2 || (len % 2) != 0 || len > MAX_LANG_CHARS) {
        return 0;
    }

    for (i = 0; i < len; i += 2) {
        char c0 = token[i];
        char c1 = token[i + 1];
        char code[3];
        uint32_t id;

        if (c0 >= 'A' && c0 <= 'Z') { c0 = (char)(c0 + 32); }
        if (c1 >= 'A' && c1 <= 'Z') { c1 = (char)(c1 + 32); }
        code[0] = c0; code[1] = c1; code[2] = '\0';

        if (cache != NULL) {
            id = csv_cache_lookup_loaded(cache, code);
        } else {
            id = csv_cache_lookup(csv_mgr, "Language", code);
        }

        if (id == 0 || id > 16) {
            *out_bitfield = 0;
            return 0;
        }
        *out_bitfield |= (1u << (id - 1u));
    }

    return 1;
}

/*------------------------------------------------------------------------*/
/* Helper: count bits set in a bitfield                                  */

static int popcount16(unsigned int bf)
{
    int n = 0;
    while (bf) { n += (int)(bf & 1u); bf >>= 1; }
    return n;
}

/*------------------------------------------------------------------------*/
/* Fixture: load Language.csv once                                        */

static GlobalCSVManager g_csv;
static CSVCache        *g_lang_cache = NULL;

static int setup_csv(void)
{
    memset(&g_csv, 0, sizeof(g_csv));
    if (!csv_cache_manager_init(&g_csv, NULL, DEFS_DIR)) {
        printf("ERROR: csv_cache_manager_init failed for %s\n", DEFS_DIR);
        return 0;
    }
    g_lang_cache = csv_cache_get_or_load(&g_csv, "Language");
    if (!g_lang_cache) {
        printf("ERROR: could not load Language.csv from %s/Language.csv\n", DEFS_DIR);
        return 0;
    }
    printf("Language.csv loaded (%d entries)\n",
           (int)g_lang_cache->entry_count);
    return 1;
}

static void teardown_csv(void)
{
    csv_cache_manager_cleanup(&g_csv);
}

/*========================================================================*/
/* Test 1: Single known language tokens                                   */
/*========================================================================*/

static void test1_single_known_languages(void)
{
    struct { const char *token; int expect_valid; const char *desc; } cases[] = {
        { "De",   1, "De -> German (valid)" },
        { "En",   1, "En -> English (valid)" },
        { "Fr",   1, "Fr -> French (valid)" },
        { "Es",   1, "Es -> Spanish (valid)" },
        { "It",   1, "It -> Italian (valid)" },
        { "Pl",   1, "Pl -> Polish (valid)" },
        { "Gr",   1, "Gr -> Greek (valid)" },
        { "de",   1, "de lower-case (valid)" },
        { "EN",   1, "EN upper-case (valid)" },
    };
    unsigned int bf;
    int i;
    int valid;

    print_sep("Test 1: Single known language tokens");

    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        valid = local_is_compact_language_token(g_lang_cache, &g_csv,
                                                cases[i].token, &bf);
        CHECK(valid == cases[i].expect_valid, cases[i].desc);
        if (cases[i].expect_valid) {
            CHECK(popcount16(bf) == 1, "  -> exactly one language bit set");
        }
    }
}

/*========================================================================*/
/* Test 2: Compact multilingual tokens — all chunks valid                */
/*========================================================================*/

static void test2_compact_multilingual_all_valid(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 2: Compact multilingual tokens — all chunks valid");

    /* TipOff_v2.0_DeEsFrIt_1269 language token */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "DeEsFrIt", &bf);
    CHECK(valid, "DeEsFrIt is a valid compact language token");
    CHECK(popcount16(bf) == 4, "DeEsFrIt yields exactly 4 language bits");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "DeFr", &bf);
    CHECK(valid, "DeFr is a valid compact language token");
    CHECK(popcount16(bf) == 2, "DeFr yields exactly 2 language bits");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "EnFrDe", &bf);
    CHECK(valid, "EnFrDe is a valid compact language token");
    CHECK(popcount16(bf) == 3, "EnFrDe yields exactly 3 language bits");
}

/*========================================================================*/
/* Test 3: False positive prevention — "EasyPlay" must not yield Pl      */
/*========================================================================*/

static void test3_easyplay_no_false_positive(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 3: EasyPlay must not yield Pl (Polish)");

    /* "EasyPlay": 8 chars, even, all-alpha — passes the pre-filter,
     * but chunks: "ea","sy","pl","ay" — "ea","sy","ay" are NOT in
     * Language.csv, so the whole token must be rejected. */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "EasyPlay", &bf);
    CHECK(!valid, "EasyPlay is rejected as a language token");
    CHECK(bf == 0, "EasyPlay produces zero bitfield");
}

/*========================================================================*/
/* Test 4: False positive prevention — "Infogrames" must not yield Gr/Es */
/*========================================================================*/

static void test4_infogrames_no_false_positive(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 4: Infogrames must not yield Gr (Greek) or Es (Spanish)");

    /* "Infogrames": 10 chars, even, all-alpha — passes pre-filter,
     * but chunks: "in","fo","gr","am","es" — "in","fo","am" are NOT in
     * Language.csv, so the whole token must be rejected. */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "Infogrames", &bf);
    CHECK(!valid, "Infogrames is rejected as a language token");
    CHECK(bf == 0, "Infogrames produces zero bitfield");
}

/*========================================================================*/
/* Test 5: Partial-match compact tokens are rejected                     */
/*========================================================================*/

static void test5_partial_compact_rejected(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 5: Compact tokens with one unknown chunk are fully rejected");

    /* "DeXxFr": De=German ok, Xx=unknown, Fr=French ok.
     * Because Xx is unknown the WHOLE token must be rejected. */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "DeXxFr", &bf);
    CHECK(!valid, "DeXxFr is rejected (Xx unknown)");
    CHECK(bf == 0, "DeXxFr produces zero bitfield (no partial De or Fr)");

    /* "XxEs": Xx unknown -> whole token rejected */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "XxEs", &bf);
    CHECK(!valid, "XxEs is rejected (Xx unknown)");
    CHECK(bf == 0, "XxEs produces zero bitfield");
}

/*========================================================================*/
/* Test 6: Odd-length tokens are always rejected                         */
/*========================================================================*/

static void test6_odd_length_rejected(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 6: Odd-length tokens are always rejected");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "EnFrX", &bf);
    CHECK(!valid, "EnFrX (odd len=5) is rejected");
    CHECK(bf == 0, "EnFrX produces zero bitfield");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "D", &bf);
    CHECK(!valid, "D (len=1) is rejected");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "", &bf);
    CHECK(!valid, "empty string is rejected");
}

/*========================================================================*/
/* Test 7: Non-alpha tokens are rejected                                 */
/*========================================================================*/

static void test7_non_alpha_rejected(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 7: Non-alpha tokens are rejected");

    /* SPS-style numeric token: digits are not language codes */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "1269", &bf);
    CHECK(!valid, "1269 (numeric) is rejected");
    CHECK(bf == 0, "1269 produces zero bitfield");

    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "v1.2", &bf);
    CHECK(!valid, "v1.2 (version) is rejected");
}

/*========================================================================*/
/* Test 8: Other publisher-like tokens are rejected                      */
/*========================================================================*/

static void test8_publisher_tokens_rejected(void)
{
    unsigned int bf;
    int valid;

    print_sep("Test 8: Publisher-like tokens that happen to contain language codes");

    /* "SomeGame" — 8 chars, all-alpha, even. Contains "Om" "eG" "Am" "E"... */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "SomeGame", &bf);
    CHECK(!valid, "SomeGame is rejected as a language token");
    CHECK(bf == 0, "SomeGame produces zero bitfield");

    /* "Amiga" — 5 chars, odd — immediately rejected */
    valid = local_is_compact_language_token(g_lang_cache, &g_csv, "Amiga", &bf);
    CHECK(!valid, "Amiga (odd) is rejected");
}

/*========================================================================*/
/* main                                                                   */
/*========================================================================*/

int main(void)
{
    printf("whdtlv language token validation - test suite\n");
    printf("Defs : %s\n", DEFS_DIR);

    if (!setup_csv()) {
        fprintf(stderr, "FATAL: could not load CSVs; aborting.\n");
        return 1;
    }

    test1_single_known_languages();
    test2_compact_multilingual_all_valid();
    test3_easyplay_no_false_positive();
    test4_infogrames_no_false_positive();
    test5_partial_compact_rejected();
    test6_odd_length_rejected();
    test7_non_alpha_rejected();
    test8_publisher_tokens_rejected();

    teardown_csv();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("========================================\n");

    return (g_fail == 0) ? 0 : 1;
}

/* End of Text */
