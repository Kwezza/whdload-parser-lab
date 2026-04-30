#ifndef VH_SCORE_H
#define VH_SCORE_H

#include "vh_parse.h"
#include "vh_profile.h"

typedef enum VhRejectCode {
    VH_REJECT_NONE = 0,
    VH_REJECT_LANGUAGE,
    VH_REJECT_CHIPSET,
    VH_REJECT_MEMORY,
    VH_REJECT_VIDEO,
    VH_REJECT_MEDIA,
    VH_REJECT_SPECIAL,
    VH_REJECT_PROFILE,
    VH_REJECT_PARSE
} VhRejectCode;

typedef struct VhScoreResult {
    long score;
    int rejected;
    VhRejectCode reject_code;
    char reject_reason[128];
} VhScoreResult;

void vh_score_candidate(const VhParsedScoreName *parsed,
                        const VhParseContext *ctx,
                        const VhProfile *profile,
                        int apply_include_weights,
                        VhScoreResult *out);

#endif
