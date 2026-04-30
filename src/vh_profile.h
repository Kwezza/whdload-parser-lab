#ifndef VH_PROFILE_H
#define VH_PROFILE_H

#include "vh_parse.h"

#define VH_PROFILE_MAX_IDS 32
#define VH_PROFILE_MAX_UNRESOLVED 32
#define VH_PROFILE_MAX_TOKEN_TEXT 64

typedef enum VhProfileField {
    VH_PROFILE_FIELD_LANGUAGE = 0,
    VH_PROFILE_FIELD_CHIPSET,
    VH_PROFILE_FIELD_VIDEO,
    VH_PROFILE_FIELD_MEMORY,
    VH_PROFILE_FIELD_MEDIA,
    VH_PROFILE_FIELD_SPECIAL,
    VH_PROFILE_FIELD_COUNT
} VhProfileField;

typedef struct VhIdList {
    int ids[VH_PROFILE_MAX_IDS];
    int count;
    char unresolved[VH_PROFILE_MAX_UNRESOLVED][VH_PROFILE_MAX_TOKEN_TEXT];
    int unresolved_count;
} VhIdList;

typedef struct VhFieldRule {
    VhIdList include;
    VhIdList exclude;
    int weight;
} VhFieldRule;

typedef struct VhProfile {
    char name[64];
    VhFieldRule rules[VH_PROFILE_FIELD_COUNT];
} VhProfile;

int vh_profile_load(VhProfile *profile, const char *path, const VhParseContext *ctx);
void vh_profile_free(VhProfile *profile);
const char *vh_profile_field_name(VhProfileField field);

#endif
