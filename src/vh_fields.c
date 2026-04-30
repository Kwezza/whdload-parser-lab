#include "vh_fields.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

typedef struct VhFieldSpec {
    VhField field;
    const char *name;
    const char *csv_filename;
} VhFieldSpec;

static int vh_char_icmp(char a, char b)
{
    return tolower((unsigned char)a) - tolower((unsigned char)b);
}

static int vh_stricmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int diff = vh_char_icmp(*a, *b);
        if (diff != 0) {
            return diff;
        }
        ++a;
        ++b;
    }
    return vh_char_icmp(*a, *b);
}

static const VhFieldSpec g_field_specs[] = {
    { VH_FIELD_MEMORY, "Memory", "Memory.csv" },
    { VH_FIELD_LANGUAGE, "Language", "Language.csv" },
    { VH_FIELD_CHIPSET, "Chipset", "Chipset.csv" },
    { VH_FIELD_VIDEO, "Video", "Video.csv" },
    { VH_FIELD_MEDIA, "Media", "Media.csv" }
};

int vh_field_from_name(const char *name, VhField *out_field)
{
    size_t i;

    if (name == NULL || out_field == NULL) {
        return 0;
    }

    for (i = 0; i < (sizeof(g_field_specs) / sizeof(g_field_specs[0])); ++i) {
        if (vh_stricmp(name, g_field_specs[i].name) == 0) {
            *out_field = g_field_specs[i].field;
            return 1;
        }
    }

    return 0;
}

const char *vh_field_display_name(VhField field)
{
    size_t i;

    for (i = 0; i < (sizeof(g_field_specs) / sizeof(g_field_specs[0])); ++i) {
        if (g_field_specs[i].field == field) {
            return g_field_specs[i].name;
        }
    }

    return "Unknown";
}

const char *vh_field_csv_filename(VhField field)
{
    size_t i;

    for (i = 0; i < (sizeof(g_field_specs) / sizeof(g_field_specs[0])); ++i) {
        if (g_field_specs[i].field == field) {
            return g_field_specs[i].csv_filename;
        }
    }

    return NULL;
}
