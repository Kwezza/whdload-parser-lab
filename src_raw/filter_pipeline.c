#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <io/writeLog.h>
#include <filter/filter_pipeline.h>

/*------------------------------------------------------------------------*/
bool filter_pipeline_init(FilterPipeline *fp) {
	if (!fp) { return false; }
	memset(fp, 0, sizeof(*fp));
	return true;
} /* filter_pipeline_init */

/*------------------------------------------------------------------------*/
void filter_pipeline_free(FilterPipeline *fp) {
	if (!fp) { return; }
	variant_index_free(&fp->vindex);
	active_set_free(&fp->active);
	if (fp->scores) { whd_free(fp->scores); fp->scores = NULL; }
	fp->variant_count = 0;
} /* filter_pipeline_free */

/*------------------------------------------------------------------------*/
uint32_t filter_pipeline_build(FilterPipeline *fp, const TLV_Record *record, FieldRegistry *reg) {
	if (!fp || !record || !reg) { return 0; }
	variant_index_free(&fp->vindex);
	if (!variant_index_build(record, reg, &fp->vindex)) { return 0; }
	fp->variant_count = fp->vindex.count;
	active_set_free(&fp->active);
	if (!active_set_init(&fp->active, fp->variant_count)) { return 0; }
	active_set_select_all(&fp->active);
	if (fp->scores) { whd_free(fp->scores); }
	fp->scores = (uint32_t*)whd_malloc(sizeof(uint32_t) * fp->variant_count);
	if (!fp->scores) { return 0; }
	memset(fp->scores, 0, sizeof(uint32_t) * fp->variant_count);
	return fp->variant_count;
} /* filter_pipeline_build */

/*------------------------------------------------------------------------*/
bool filter_pipeline_apply_profile(FilterPipeline *fp, const FilterProfile *profile) {
	if (!fp || !profile || fp->variant_count == 0) { return false; }
	for (uint32_t i = 0; i < fp->variant_count; i++) {
		bool rejected = false;
		uint32_t score = filter_score_variant(profile, &fp->vindex, i, &rejected);
		fp->scores[i] = score;
		if (rejected) {
			/* Clear bit */
			if (i < fp->active.total) {
				uint32_t word = i / 64u; uint32_t bit = i % 64u;
				if (word < fp->active.bit_capacity) {
					uint64_t mask = ((uint64_t)1) << bit;
					if (fp->active.bits[word] & mask) {
						fp->active.bits[word] &= ~mask; fp->active.active_count--;
						fp->active.dirty_dense = true;
					}
				}
			}
		}
	}
	return true;
} /* filter_pipeline_apply_profile */

/*------------------------------------------------------------------------*/
#ifdef _GNU_SOURCE
static int cmp_score_desc(const void *a, const void *b, void *ctx) {
	const FilterPipeline *fp = (const FilterPipeline*)ctx;
	uint32_t ia = *(const uint32_t*)a;
	uint32_t ib = *(const uint32_t*)b;
	uint32_t sa = fp->scores[ia];
	uint32_t sb = fp->scores[ib];
	if (sa == sb) { return (ia < ib) ? -1 : (ia > ib); }
	return (sa > sb) ? -1 : 1;
}
#endif

const uint32_t *filter_pipeline_get_sorted(const FilterPipeline *fp, uint32_t *out_count) {
	if (!fp) { return NULL; }
	TLV_ActiveSet *mutable_set = (TLV_ActiveSet*)&fp->active; /* reuse active set for sort buffer */
	const uint32_t *dense = active_set_build_dense(mutable_set);
	if (!dense) { return NULL; }
	uint32_t n = mutable_set->active_count;
	/* qsort_r not portable everywhere; implement simple shell insertion if not available */
#ifdef _GNU_SOURCE
	qsort_r(mutable_set->dense, n, sizeof(uint32_t), cmp_score_desc, (void*)fp);
#else
	/* Simple insertion sort (n usually small enough after filtering) */
	for (uint32_t i = 1; i < n; i++) {
		uint32_t key = mutable_set->dense[i];
		uint32_t j = i;
		while (j > 0) {
			uint32_t prev = mutable_set->dense[j-1];
			uint32_t sp = fp->scores[prev];
			uint32_t sk = fp->scores[key];
			if (sp > sk || (sp == sk && prev < key)) { break; }
			mutable_set->dense[j] = prev; j--;
		}
		mutable_set->dense[j] = key;
	}
#endif
	if (out_count) { *out_count = n; }
	return mutable_set->dense;
} /* filter_pipeline_get_sorted */

/*------------------------------------------------------------------------*/
/* Apply field-based filtering from parameters (convenience wrapper) */
bool filter_pipeline_apply_field_filter(FilterPipeline *fp,
					 const FieldRegistry *reg, GlobalCSVManager *csv,
					 const char *field_name,
					 const char *include_list,
					 const char *exclude_list) {
	if (!fp || !reg || !field_name) { return false; }

	uint32_t filtered = active_set_filter_by_field(&fp->active, &fp->vindex,
		reg, csv, field_name, include_list, exclude_list);

	/* Update cached count */
	return (filtered == fp->active.active_count);
} /* filter_pipeline_apply_field_filter */

/*------------------------------------------------------------------------*/
/* Combined structural + text filtering */
uint32_t filter_pipeline_apply_search(FilterPipeline *fp,
				       const char *search_text,
				       uint32_t *result_indices,
				       uint32_t max_results) {
	if (!fp || !search_text || !result_indices) { return 0; }

	return active_set_search_text(&fp->active, &fp->vindex,
		search_text, result_indices, max_results);
} /* filter_pipeline_apply_search */

/*------------------------------------------------------------------------*/
/* Enhanced Search Functions for Step 2 */

uint32_t filter_pipeline_search_enhanced(FilterPipeline *fp,
					  const char *search_text,
					  SearchMode mode,
					  SearchResultSet *results) {
	if (!fp || !search_text || !results) { return 0; }

	return active_set_search_enhanced(&fp->active, &fp->vindex,
		search_text, mode, results);
} /* filter_pipeline_search_enhanced */

/*------------------------------------------------------------------------*/
uint32_t filter_pipeline_search_multi_term(FilterPipeline *fp,
					    const char *search_terms,
					    bool require_all_terms,
					    SearchResultSet *results) {
	if (!fp || !search_terms || !results) { return 0; }

	return active_set_search_multi_term(&fp->active, &fp->vindex,
		search_terms, require_all_terms, results);
} /* filter_pipeline_search_multi_term */

/*------------------------------------------------------------------------*/
/* Smart search that automatically selects mode based on input pattern */
uint32_t filter_pipeline_search_auto(FilterPipeline *fp,
				      const char *search_input,
				      SearchResultSet *results) {
	if (!fp || !search_input || !*search_input || !results) { return 0; }

	/* Analyze search input to choose best mode */
	SearchMode mode = SEARCH_MODE_CONTAINS; /* Default */
	bool multi_term = false;

	/* Check for multiple terms (contains spaces) */
	if (strchr(search_input, ' ') != NULL) {
		multi_term = true;
	}

	/* Check for exact match indicators (quotes) */
	if (search_input[0] == '"' && search_input[strlen(search_input)-1] == '"') {
		mode = SEARCH_MODE_EXACT;
		/* Remove quotes for search */
		char clean_input[256];
		size_t len = strlen(search_input);
		if (len > 2 && len < sizeof(clean_input)) {
			strncpy(clean_input, search_input + 1, len - 2);
			clean_input[len - 2] = '\0';
			return active_set_search_enhanced(&fp->active, &fp->vindex,
				clean_input, mode, results);
		}
	}

	/* Check for starts-with indicator (^prefix) */
	if (search_input[0] == '^' && strlen(search_input) > 1) {
		mode = SEARCH_MODE_STARTS_WITH;
		return active_set_search_enhanced(&fp->active, &fp->vindex,
			search_input + 1, mode, results);
	}

	/* Use multi-term search for multiple words */
	if (multi_term) {
		/* Default to OR mode (any term matches) for better user experience */
		return active_set_search_multi_term(&fp->active, &fp->vindex,
			search_input, false, results);
	}

	/* Single term search */
	return active_set_search_enhanced(&fp->active, &fp->vindex,
		search_input, mode, results);
} /* filter_pipeline_search_auto */

/*------------------------------------------------------------------------*/
/* End of Text */
