#ifndef FILTER_FILTER_PIPELINE_H
#define FILTER_FILTER_PIPELINE_H

#include <platform.h>
#include <filter/variant_index.h>
#include <filter/active_set.h>
#include <filter/filter_profile.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/**
 * High-level pipeline: build variant index from TLV, initialise active set,
 * apply profile-based include/exclude scoring pass producing an ordered list.
 */

typedef struct {
	TLV_VariantIndex vindex;      /* built variant index */
	TLV_ActiveSet active;         /* active (non-rejected) variants */
	uint32_t *scores;             /* per-variant scores (size = variant_count) */
	uint32_t variant_count;       /* cached count */
} FilterPipeline;

/* Initialise empty pipeline (scores NULL). */
bool filter_pipeline_init(FilterPipeline *fp);
/* Free all internal resources. */
void filter_pipeline_free(FilterPipeline *fp);

/* Build index + active set from TLV record. Returns variant count or 0 on error. */
uint32_t filter_pipeline_build(FilterPipeline *fp, const TLV_Record *record, FieldRegistry *reg);

/* Apply profile scoring; inactive variants get score 0 and are deselected if rejected. */
bool filter_pipeline_apply_profile(FilterPipeline *fp, const FilterProfile *profile);

/* Produce dense ordered list of active variants sorted by score (desc). */
const uint32_t *filter_pipeline_get_sorted(const FilterPipeline *fp, uint32_t *out_count);

/* Apply field-based filtering from parameters (convenience wrapper) */
bool filter_pipeline_apply_field_filter(FilterPipeline *fp,
					 const FieldRegistry *reg, GlobalCSVManager *csv,
					 const char *field_name,
					 const char *include_list,
					 const char *exclude_list);

/* Combined structural + text filtering */
uint32_t filter_pipeline_apply_search(FilterPipeline *fp,
				       const char *search_text,
				       uint32_t *result_indices,
				       uint32_t max_results);

/* Enhanced Search Functions for Step 2 */

/* Enhanced search with multiple modes and relevance scoring */
uint32_t filter_pipeline_search_enhanced(FilterPipeline *fp,
					  const char *search_text,
					  SearchMode mode,
					  SearchResultSet *results);

/* Multi-term search with AND/OR logic */
uint32_t filter_pipeline_search_multi_term(FilterPipeline *fp,
					    const char *search_terms,
					    bool require_all_terms,
					    SearchResultSet *results);

/* Search with automatic mode selection based on search pattern */
uint32_t filter_pipeline_search_auto(FilterPipeline *fp,
				      const char *search_input,
				      SearchResultSet *results);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_FILTER_PIPELINE_H */
