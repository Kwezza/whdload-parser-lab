/* filter_profile.h - Phase 4 filter profile parsing and scoring interface
 *
 * Defines structures and APIs for include/exclude token lists per field,
 * weight handling, and variant scoring. Initial implementation focuses on
 * lightweight integration with existing VariantIndex and ActiveSet.
 */
#ifndef FILTER_FILTER_PROFILE_H
#define FILTER_FILTER_PROFILE_H

#include <platform.h>
#include <tlv_filename/field_registry.h>
#include <filter/variant_index.h>
#include <tlv_filename/csv_cache.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FP_MAX_FIELDS 16            /* configurable upper bound */
#define FP_MAX_INCLUDE 32           /* per-field include list limit */
#define FP_MAX_EXCLUDE 32           /* per-field exclude list limit */

typedef struct {
	uint8_t field_id;                /* registry field id */
	uint8_t include_count;           /* number of include ids */
	uint8_t exclude_count;           /* number of exclude ids */
	uint8_t weight;                  /* weighting factor (1..255, 0=disabled) */
	uint8_t rank_by_id[256];         /* simple rank table (0xFF = not included) */
	uint16_t include_ids[FP_MAX_INCLUDE];
	uint16_t exclude_ids[FP_MAX_EXCLUDE];
	bool allow_multiple;             /* field can appear multiple times */
	bool in_use;                     /* profile entry active */
} FP_FieldProfile;

typedef struct {
	FP_FieldProfile fields[FP_MAX_FIELDS];
	uint8_t field_count;             /* active slots */
	bool debug_enabled;              /* emit verbose diagnostics */
	const FieldRegistry *reg;        /* registry for name lookup */
	GlobalCSVManager *csv_mgr;       /* csv manager for token mapping */
} FilterProfile;

/* Detailed per-variant scoring metadata (optional) */
typedef struct {
	uint8_t field_ranks[FP_MAX_FIELDS];   /* 0..include_count-1, 0xFF = no match */
	uint16_t field_tokens[FP_MAX_FIELDS]; /* effective token id chosen (0 if none) */
} FP_ScoreDetail;

/* Initialise empty profile */
void filter_profile_init(FilterProfile *pf);

/* Parse profile from INI-like preferences (stub: will later consult prefs system) */
bool filter_profile_load_defaults(FilterProfile *pf, const FieldRegistry *reg, GlobalCSVManager *csv_mgr);

/* Compute score for one variant; rejected set true if excluded */
uint32_t filter_score_variant(const FilterProfile *pf,
				const TLV_VariantIndex *vindex,
				uint32_t variant_index,
				bool *rejected);

/* Extended scoring with per-field rank/token capture */
uint32_t filter_score_variant_detailed(const FilterProfile *pf,
				const TLV_VariantIndex *vindex,
				uint32_t variant_index,
				bool *rejected,
				FP_ScoreDetail *detail);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_FILTER_PROFILE_H */
/* End of Text */
