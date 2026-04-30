#ifndef VH_GROUP_H
#define VH_GROUP_H

#include "vh_parse.h"
#include "vh_profile.h"
#include "vh_score.h"
#include "vh_string_pool.h"

#define VH_MAX_REJECT_REASON 128

#define VH_CAND_LITE_FLAG_SELECTED   0x01
#define VH_CAND_LITE_FLAG_REJECTED   0x02
#define VH_CAND_LITE_FLAG_SINGLETON  0x04
#define VH_CAND_LITE_FLAG_SPECIAL    0x08

typedef enum VhCandidateLiteRejectCode {
    VH_CAND_LITE_REJECT_NONE = 0,
    VH_CAND_LITE_REJECT_LANGUAGE,
    VH_CAND_LITE_REJECT_CHIPSET,
    VH_CAND_LITE_REJECT_MEMORY,
    VH_CAND_LITE_REJECT_VIDEO,
    VH_CAND_LITE_REJECT_MEDIA,
    VH_CAND_LITE_REJECT_SPECIAL,
    VH_CAND_LITE_REJECT_PROFILE
} VhCandidateLiteRejectCode;

/* Phase 3 compact candidate structure for selector-memory projection. */
typedef struct VhCandidateLite {
    unsigned long name_off;
    unsigned long group_hash;
    unsigned long group_off;
    unsigned int original_index;
    short score;
    unsigned char flags;
    unsigned char reject_reason;
} VhCandidateLite;

typedef struct VhCandidate {
    unsigned long archive_name_off;
    unsigned long group_key_off;
    unsigned long group_hash;
    int original_index;
    int selected;
    int rejected;
    VhRejectCode reject_code;
    long score;
    char reject_reason[VH_MAX_REJECT_REASON];
} VhCandidate;

typedef struct VhCandidateList {
    VhCandidate *items;
    int count;
    int capacity;
    VhStringPool strings;
    const VhParseContext *parse_ctx;
} VhCandidateList;

int vh_group_load_candidates(VhCandidateList *list, const char *listfile, const VhParseContext *ctx);
void vh_group_free_candidates(VhCandidateList *list);
int vh_group_select_best(VhCandidateList *list, const VhProfile *profile);
void vh_group_print_selected(const VhCandidateList *list);
void vh_group_print_report(const VhCandidateList *list);
void vh_group_calculate_stats(const VhCandidateList *list,
                              int *out_group_count,
                              int *out_duplicate_group_count,
                              int *out_largest_duplicate_group_size,
                              int *out_selected_count);

#endif
