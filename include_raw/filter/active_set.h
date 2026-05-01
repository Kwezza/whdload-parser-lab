/* active_set.h - Phase 3 Active Variant Set & basic filtering
 *
 * Maintains a selectable subset of variants using a bitset plus optional
 * dense index list for iteration. Designed for lightweight filtering
 * operations (substring match, predicate on descriptor properties) prior
 * to scoring/ranking phases.
 */
#ifndef FILTER_ACTIVE_SET_H
#define FILTER_ACTIVE_SET_H

#include <platform.h>
#include <filter/variant_index.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/csv_cache.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t total;          /* total variants available */
	uint32_t active_count;   /* number of active variants */
	uint64_t *bits;          /* bitset array (ceil(total/64)) */
	uint32_t bit_capacity;   /* number of 64-bit slots */
	uint32_t *dense;         /* optional dense list (allocated on demand) */
	uint32_t dense_capacity; /* slots allocated */
	bool dirty_dense;        /* true if dense list needs rebuild */
} TLV_ActiveSet;

bool active_set_init(TLV_ActiveSet *set, uint32_t total);
void active_set_free(TLV_ActiveSet *set);
void active_set_select_all(TLV_ActiveSet *set);
void active_set_clear(TLV_ActiveSet *set);

/* Build / rebuild dense list (returns pointer to internal array) */
const uint32_t *active_set_build_dense(TLV_ActiveSet *set);

/* Apply case-insensitive ASCII substring filter on display_name; keeps matches */
uint32_t active_set_filter_name_contains(TLV_ActiveSet *set,
					 const TLV_VariantIndex *index,
					 const char *substr);

/* Apply interior field count threshold (keeps variants with >= min_fields) */
uint32_t active_set_filter_min_fields(TLV_ActiveSet *set,
				       const TLV_VariantIndex *index,
				       uint32_t min_fields);

/* Apply field-based include/exclude filtering (core structural filtering) */
uint32_t active_set_filter_by_field(TLV_ActiveSet *set,
				     const TLV_VariantIndex *index,
				     const FieldRegistry *registry,
				     GlobalCSVManager *csv,
				     const char *field_name,
				     const char *include_list,
				     const char *exclude_list);

/* Search text within active variants only, return result indices array */
uint32_t active_set_search_text(const TLV_ActiveSet *active,
				 const TLV_VariantIndex *vindex,
				 const char *search_text,
				 uint32_t *result_indices,
				 uint32_t max_results);

/* Enhanced Search Structures for Step 2 */
typedef enum {
	SEARCH_MODE_CONTAINS = 0,    /* Default: substring match */
	SEARCH_MODE_STARTS_WITH = 1, /* Match at beginning */
	SEARCH_MODE_EXACT = 2,       /* Exact match */
	SEARCH_MODE_ALL_TERMS = 3    /* All terms must match (AND) */
} SearchMode;

typedef struct {
	uint32_t variant_index;      /* Index into variant array */
	uint32_t relevance_score;    /* Higher = more relevant */
	uint32_t match_count;        /* Number of search term matches */
	uint32_t display_name_matches; /* Matches in display name */
	uint32_t filename_matches;   /* Matches in filename */
} SearchResult;

typedef struct {
	SearchResult *results;       /* Array of search results */
	uint32_t count;             /* Number of results found */
	uint32_t capacity;          /* Allocated capacity */
	uint32_t total_searched;    /* Total variants searched */
	uint32_t search_time_ms;    /* Search time in milliseconds */
} SearchResultSet;

/* Enhanced search functions for Step 2 */
bool search_result_set_init(SearchResultSet *results, uint32_t capacity);
void search_result_set_free(SearchResultSet *results);
void search_result_set_clear(SearchResultSet *results);

/* Enhanced text search with multiple modes and scoring */
uint32_t active_set_search_enhanced(const TLV_ActiveSet *active,
				     const TLV_VariantIndex *vindex,
				     const char *search_text,
				     SearchMode mode,
				     SearchResultSet *results);

/* Search with multiple terms (space-separated) */
uint32_t active_set_search_multi_term(const TLV_ActiveSet *active,
				       const TLV_VariantIndex *vindex,
				       const char *search_terms,
				       bool require_all_terms,
				       SearchResultSet *results);

/* Get top N results sorted by relevance */
const SearchResult *search_result_set_get_top(const SearchResultSet *results,
					       uint32_t max_count,
					       uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_ACTIVE_SET_H */
/* End of Text */
