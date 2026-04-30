#include <stdio.h>
#include <string.h>

#include "../src/vh_parse.h"

typedef struct VhCase {
    const char *archive_name;
    const char *expected_key;
} VhCase;

static int assert_key_case(const VhCase *c)
{
    char key[128];
    unsigned long hash;

    if (!vh_parse_group_key(c->archive_name, key, sizeof(key), &hash)) {
        fprintf(stderr, "vh_parse_group_key failed for '%s'\n", c->archive_name);
        return 0;
    }

    if (strcmp(key, c->expected_key) != 0) {
        fprintf(stderr,
                "group key mismatch for '%s': got '%s' expected '%s'\n",
                c->archive_name,
                key,
                c->expected_key);
        return 0;
    }

    if (hash == 0UL) {
        fprintf(stderr, "group hash should be non-zero for '%s'\n", c->archive_name);
        return 0;
    }

    return 1;
}

static int assert_matches_full_parser(const char *archive_name, const VhParseContext *ctx)
{
    VhParsedName parsed;
    char key[128];
    unsigned long hash;

    if (!vh_parse_filename(ctx, archive_name, &parsed)) {
        fprintf(stderr, "vh_parse_filename failed for '%s'\n", archive_name);
        return 0;
    }

    if (!vh_parse_group_key(archive_name, key, sizeof(key), &hash)) {
        fprintf(stderr, "vh_parse_group_key failed for '%s'\n", archive_name);
        return 0;
    }

    if (strcmp(parsed.group_key, key) != 0) {
        fprintf(stderr,
                "lightweight/full parser group key mismatch for '%s': '%s' vs '%s'\n",
                archive_name,
                key,
                parsed.group_key);
        return 0;
    }

    if (hash == 0UL) {
        fprintf(stderr, "group hash should be non-zero for '%s'\n", archive_name);
        return 0;
    }

    return 1;
}

int main(void)
{
    const VhCase cases[] = {
        {"Game_v1.0.lha", "game"},
        {"GameDemo_v1.0.lha", "gamedemo"},
        {"R-Type_v1.0_En.lha", "rtype"},
        {"A10TankKiller_v2.0_2Disk.lha", "a10tankkiller"}
    };
    const char *comparison_cases[] = {
        "Game_v1.0.lha",
        "GameDemo_v1.0.lha",
        "Game2_v1.0.lha",
        "GameDataDisk_v1.0.lha",
        "GameEditor_v1.0.lha",
        "Lionheart_v1.2_AGA_En.lha",
        "Some-Game_(Demo)_v1.0_NTSC.lha"
    };
    VhParseContext ctx;
    size_t i;

    if (!vh_parse_context_load(&ctx, "data/Defs")) {
        fprintf(stderr, "Failed to load parser CSV definitions from data/Defs\n");
        return 1;
    }

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!assert_key_case(&cases[i])) {
            vh_parse_context_free(&ctx);
            return 1;
        }
    }

    for (i = 0; i < sizeof(comparison_cases) / sizeof(comparison_cases[0]); ++i) {
        if (!assert_matches_full_parser(comparison_cases[i], &ctx)) {
            vh_parse_context_free(&ctx);
            return 1;
        }
    }

    vh_parse_context_free(&ctx);
    printf("vh_parse_group_key tests passed\n");
    return 0;
}
