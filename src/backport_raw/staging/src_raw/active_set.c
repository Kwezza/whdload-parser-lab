#include <platform.h>
#include <filter/active_set.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/csv_cache.h>
#include <string.h>
#include <ctype.h>

static inline void set_bit(uint64_t *bits, uint32_t idx) { bits[idx >> 6] |= (uint64_t)1ULL << (idx & 63); }
static inline void clr_bit(uint64_t *bits, uint32_t idx) { bits[idx >> 6] &= ~((uint64_t)1ULL << (idx & 63)); }
static inline bool tst_bit(const uint64_t *bits, uint32_t idx) { return (bits[idx >> 6] >> (idx & 63)) & 1ULL; }

bool active_set_init(TLV_ActiveSet *set, uint32_t total) {
	if (!set) { return false; }
	memset(set, 0, sizeof(*set));
	set->total = total;
	uint32_t slots = (total + 63u) / 64u;
	if (slots == 0) { return true; }
	set->bits = (uint64_t*)whd_malloc(sizeof(uint64_t) * slots);
	if (!set->bits) { return false; }
	memset(set->bits, 0xFF, sizeof(uint64_t) * slots); /* default all selected */
	set->bit_capacity = slots;
	set->active_count = total;
	set->dirty_dense = true;
	return true;
} /* active_set_init */
/*------------------------------------------------------------------------*/

void active_set_free(TLV_ActiveSet *set) {
	if (!set) { return; }
	if (set->bits) { whd_free(set->bits); }
	if (set->dense) { whd_free(set->dense); }
	memset(set, 0, sizeof(*set));
} /* active_set_free */
/*------------------------------------------------------------------------*/

void active_set_select_all(TLV_ActiveSet *set) {
	if (!set || !set->bits) { return; }
	for (uint32_t i = 0; i < set->bit_capacity; i++) { set->bits[i] = 0xFFFFFFFFFFFFFFFFULL; }
	/* Mask off spare bits in final slot */
	if (set->total) {
		uint32_t spare = (set->bit_capacity * 64u) - set->total;
		if (spare) { set->bits[set->bit_capacity - 1] >>= spare; set->bits[set->bit_capacity - 1] <<= spare; set->bits[set->bit_capacity - 1] |= ((1ULL << (64 - spare)) - 1ULL); }
	}
	set->active_count = set->total;
	set->dirty_dense = true;
} /* active_set_select_all */
/*------------------------------------------------------------------------*/

void active_set_clear(TLV_ActiveSet *set) {
	if (!set || !set->bits) { return; }
	memset(set->bits, 0, sizeof(uint64_t)*set->bit_capacity);
	set->active_count = 0;
	set->dirty_dense = true;
} /* active_set_clear */
/*------------------------------------------------------------------------*/

const uint32_t *active_set_build_dense(TLV_ActiveSet *set) {
	if (!set) { return NULL; }
	if (!set->dirty_dense && set->dense) { return set->dense; }
	if (set->dense_capacity < set->active_count) {
		uint32_t new_cap = set->active_count ? set->active_count : 16;
		uint32_t *np = (uint32_t*)whd_malloc(sizeof(uint32_t)*new_cap);
		if (!np) { return NULL; }
		if (set->dense) { whd_free(set->dense); }
		set->dense = np; set->dense_capacity = new_cap;
	}
	uint32_t w = 0;
	for (uint32_t i = 0; i < set->total; i++) {
		if (tst_bit(set->bits, i)) { if (w < set->active_count) { set->dense[w++] = i; } }
	}
	set->dirty_dense = false;
	return set->dense;
} /* active_set_build_dense */
/*------------------------------------------------------------------------*/

static uint32_t finalize_recount(TLV_ActiveSet *set) {
	uint32_t cnt = 0;
	for (uint32_t i = 0; i < set->total; i++) { if (tst_bit(set->bits, i)) { cnt++; } }
	set->active_count = cnt;
	set->dirty_dense = true;
	return cnt;
} /* finalize_recount */
/*------------------------------------------------------------------------*/

uint32_t active_set_filter_name_contains(TLV_ActiveSet *set, const TLV_VariantIndex *index, const char *substr) {
	if (!set || !index || !substr || !*substr) { return set ? set->active_count : 0; }
	/* Lowercase search token */
	char token[128]; size_t tlen = 0; while (substr[tlen] && tlen < sizeof(token)-1) { char c = substr[tlen]; if (c >= 'A' && c <= 'Z') { c = (char)(c - 'A' + 'a'); } token[tlen] = c; tlen++; } token[tlen] = '\0';
	for (uint32_t i = 0; i < index->count && i < set->total; i++) {
		if (!tst_bit(set->bits, i)) { continue; }
		const char *name = index->items[i].display_name ? index->items[i].display_name : "";
		/* Case-insensitive search */
		bool match = false;
		for (size_t pos = 0; name[pos]; pos++) {
			char c = name[pos]; if (c >= 'A' && c <= 'Z') { c = (char)(c - 'A' + 'a'); }
			if (c == token[0]) {
				/* quick compare */
				size_t k = 0; while (token[k] && name[pos + k]) {
					char nc = name[pos + k]; if (nc >= 'A' && nc <= 'Z') { nc = (char)(nc - 'A' + 'a'); }
					if (token[k] != nc) { break; }
					k++;
				}
				if (!token[k]) { match = true; break; }
			}
		}
		if (!match) { clr_bit(set->bits, i); }
	}
	return finalize_recount(set);
} /* active_set_filter_name_contains */
/*------------------------------------------------------------------------*/

uint32_t active_set_filter_min_fields(TLV_ActiveSet *set, const TLV_VariantIndex *index, uint32_t min_fields) {
	if (!set || !index) { return 0; }
	for (uint32_t i = 0; i < index->count && i < set->total; i++) {
		if (!tst_bit(set->bits, i)) { continue; }
		if (index->items[i].interior_fields < min_fields) { clr_bit(set->bits, i); }
	}
	return finalize_recount(set);
} /* active_set_filter_min_fields */
/*------------------------------------------------------------------------*/

/* Apply field-based include/exclude filtering (core structural filtering) */
uint32_t active_set_filter_by_field(TLV_ActiveSet *set,
				     const TLV_VariantIndex *index,
				     const FieldRegistry *registry,
				     GlobalCSVManager *csv,
				     const char *field_name,
				     const char *include_list,
				     const char *exclude_list) {
	if (!set || !index || !registry || !field_name) {
		return set ? set->active_count : 0;
	}

	uint8_t field_id = field_registry_get_id(registry, field_name);
	if (!field_id) {
		return set->active_count; /* Unknown field, no filtering */
	}

	const char *csv_file = get_csv_filename_for_field(registry, field_name);
	if (!csv_file) {
		return set->active_count; /* No CSV file for field */
	}

	/* Parse include list */
	uint16_t include_ids[32];
	uint32_t include_count = 0;
	if (include_list && *include_list) {
		char temp[256];
		strncpy(temp, include_list, sizeof(temp)-1);
		temp[sizeof(temp)-1] = '\0';

		char *token = strtok(temp, ",");
		while (token && include_count < 32) {
			/* Trim whitespace */
			while (*token == ' ' || *token == '\t') token++;
			char *end = token + strlen(token) - 1;
			while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';

			if (*token) {
				uint32_t csv_id = csv_cache_lookup(csv, csv_file, token);
				if (csv_id > 0) {
					include_ids[include_count++] = (uint16_t)csv_id;
				}
			}
			token = strtok(NULL, ",");
		}
	}

	/* Parse exclude list */
	uint16_t exclude_ids[32];
	uint32_t exclude_count = 0;
	if (exclude_list && *exclude_list) {
		char temp[256];
		strncpy(temp, exclude_list, sizeof(temp)-1);
		temp[sizeof(temp)-1] = '\0';

		char *token = strtok(temp, ",");
		while (token && exclude_count < 32) {
			/* Trim whitespace */
			while (*token == ' ' || *token == '\t') token++;
			char *end = token + strlen(token) - 1;
			while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';

			if (*token) {
				uint32_t csv_id = csv_cache_lookup(csv, csv_file, token);
				if (csv_id > 0) {
					exclude_ids[exclude_count++] = (uint16_t)csv_id;
				}
			}
			token = strtok(NULL, ",");
		}
	}

	/* Apply filtering to active set */
	for (uint32_t i = 0; i < index->count && i < set->total; i++) {
		if (!tst_bit(set->bits, i)) { continue; } /* Already filtered out */

		const TLV_VariantDescriptor *vd = &index->items[i];
		bool variant_active = true;
		bool include_match = (include_count == 0); /* If no includes, default to pass */
		bool exclude_match = false;

		/* Check all tokens for this variant */
		for (uint8_t tk = 0; tk < vd->token_count; tk++) {
			if (vd->tokens[tk].field_id != field_id) continue;

			uint16_t token_id = (uint16_t)vd->tokens[tk].csv_id;

			/* Check include list */
			if (include_count > 0) {
				for (uint32_t inc = 0; inc < include_count; inc++) {
					if (token_id == include_ids[inc]) {
						include_match = true;
						break;
					}
				}
			}

			/* Check exclude list */
			for (uint32_t exc = 0; exc < exclude_count; exc++) {
				if (token_id == exclude_ids[exc]) {
					exclude_match = true;
					break;
				}
			}
		}

		/* Apply filtering logic */
		if (exclude_match) {
			variant_active = false; /* Exclude takes precedence */
		} else if (include_count > 0 && !include_match) {
			variant_active = false; /* Must match include list if specified */
		}

		/* Update active set */
		if (!variant_active) {
			clr_bit(set->bits, i);
		}
	}

	return finalize_recount(set);
} /* active_set_filter_by_field */
/*------------------------------------------------------------------------*/

/* Search text within active variants only, return result indices array */
uint32_t active_set_search_text(const TLV_ActiveSet *active,
				 const TLV_VariantIndex *vindex,
				 const char *search_text,
				 uint32_t *result_indices,
				 uint32_t max_results) {
	if (!active || !vindex || !search_text || !*search_text || !result_indices) {
		return 0;
	}

	/* Convert search text to lowercase */
	char lower_search[128];
	strncpy(lower_search, search_text, sizeof(lower_search)-1);
	lower_search[sizeof(lower_search)-1] = '\0';
	for (size_t i = 0; lower_search[i]; i++) {
		if (lower_search[i] >= 'A' && lower_search[i] <= 'Z') {
			lower_search[i] = (char)(lower_search[i] - 'A' + 'a');
		}
	}

	uint32_t result_count = 0;

	/* Build dense list of active variants */
	TLV_ActiveSet *mutable_active = (TLV_ActiveSet*)active; /* For dense list building */
	const uint32_t *dense = active_set_build_dense(mutable_active);
	if (!dense) {
		return 0;
	}

	/* Search only within active variants */
	for (uint32_t i = 0; i < active->active_count && result_count < max_results; i++) {
		uint32_t variant_idx = dense[i];
		if (variant_idx >= vindex->count) continue;

		const TLV_VariantDescriptor *vd = &vindex->items[variant_idx];
		if (!vd->display_name) continue;

		/* Convert variant name to lowercase and search */
		char lower_name[256];
		strncpy(lower_name, vd->display_name, sizeof(lower_name)-1);
		lower_name[sizeof(lower_name)-1] = '\0';
		for (size_t j = 0; lower_name[j]; j++) {
			if (lower_name[j] >= 'A' && lower_name[j] <= 'Z') {
				lower_name[j] = (char)(lower_name[j] - 'A' + 'a');
			}
		}

		if (strstr(lower_name, lower_search)) {
			result_indices[result_count++] = variant_idx;
		}
	}

	return result_count;
} /* active_set_search_text */

/*------------------------------------------------------------------------*/
/* Enhanced Search Functions - Step 2 Implementation */

bool search_result_set_init(SearchResultSet *results, uint32_t capacity) {
	if (!results) { return false; }
	memset(results, 0, sizeof(*results));
	if (capacity == 0) { return true; }

	results->results = (SearchResult*)whd_malloc(sizeof(SearchResult) * capacity);
	if (!results->results) { return false; }

	results->capacity = capacity;
	results->count = 0;
	return true;
} /* search_result_set_init */

/*------------------------------------------------------------------------*/
void search_result_set_free(SearchResultSet *results) {
	if (!results) { return; }
	if (results->results) {
		whd_free(results->results);
	}
	memset(results, 0, sizeof(*results));
} /* search_result_set_free */

/*------------------------------------------------------------------------*/
void search_result_set_clear(SearchResultSet *results) {
	if (!results) { return; }
	results->count = 0;
	results->total_searched = 0;
	results->search_time_ms = 0;
} /* search_result_set_clear */

/*------------------------------------------------------------------------*/
/* Helper function to convert string to lowercase */
static void str_to_lower(char *dest, const char *src, size_t max_len) {
	size_t i;
	for (i = 0; i < max_len - 1 && src[i]; i++) {
		if (src[i] >= 'A' && src[i] <= 'Z') {
			dest[i] = (char)(src[i] - 'A' + 'a');
		} else {
			dest[i] = src[i];
		}
	}
	dest[i] = '\0';
} /* str_to_lower */

/*------------------------------------------------------------------------*/
/* Helper function to count substring matches */
static uint32_t count_substring_matches(const char *text, const char *search) {
	if (!text || !search || !*search) { return 0; }

	uint32_t count = 0;
	const char *pos = text;
	size_t search_len = strlen(search);

	while ((pos = strstr(pos, search)) != NULL) {
		count++;
		pos += search_len;
	}
	return count;
} /* count_substring_matches */

/*------------------------------------------------------------------------*/
/* Calculate relevance score for a search match */
static uint32_t calculate_relevance_score(const TLV_VariantDescriptor *vd,
					   const char *search_text,
					   SearchMode mode) {
	if (!vd || !search_text || !*search_text) { return 0; }

	char lower_search[128];
	str_to_lower(lower_search, search_text, sizeof(lower_search));

	uint32_t score = 0;

	/* Check display name */
	if (vd->display_name) {
		char lower_display[256];
		str_to_lower(lower_display, vd->display_name, sizeof(lower_display));

		switch (mode) {
		case SEARCH_MODE_EXACT:
			if (strcmp(lower_display, lower_search) == 0) {
				score += 1000; /* Exact match gets highest score */
			}
			break;
		case SEARCH_MODE_STARTS_WITH:
			if (strncmp(lower_display, lower_search, strlen(lower_search)) == 0) {
				score += 800; /* Starts with gets high score */
			}
			break;
		case SEARCH_MODE_CONTAINS:
		case SEARCH_MODE_ALL_TERMS:
		default:
			{
				uint32_t matches = count_substring_matches(lower_display, lower_search);
				score += matches * 100; /* Multiple matches increase score */
				if (matches > 0) {
					/* Bonus for matches at beginning */
					if (strncmp(lower_display, lower_search, strlen(lower_search)) == 0) {
						score += 200;
					}
					/* Penalty for very long names (less relevant) */
					size_t name_len = strlen(lower_display);
					if (name_len > 50) {
						score = (score * 80) / 100; /* 20% penalty */
					}
				}
			}
			break;
		}
	}

	/* Check filename if no display name match */
	if (score == 0) {
		/* For now, we only search in display_name since that's what's available
		 * in TLV_VariantDescriptor. Future enhancement could add original
		 * filename access through TLV entries */
		/* No additional filename search available */
	}

	return score;
} /* calculate_relevance_score */

/*------------------------------------------------------------------------*/
/* Add result to result set (maintains sorted order by relevance) */
static void add_search_result(SearchResultSet *results,
			      uint32_t variant_idx,
			      uint32_t relevance_score,
			      uint32_t display_matches,
			      uint32_t filename_matches) {
	if (!results || results->count >= results->capacity) { return; }

	SearchResult new_result = {
		.variant_index = variant_idx,
		.relevance_score = relevance_score,
		.match_count = display_matches + filename_matches,
		.display_name_matches = display_matches,
		.filename_matches = filename_matches
	};

	/* Insert in sorted order (highest relevance first) */
	uint32_t insert_pos = results->count;
	for (uint32_t i = 0; i < results->count; i++) {
		if (results->results[i].relevance_score < relevance_score) {
			insert_pos = i;
			break;
		}
	}

	/* Shift existing results down */
	for (uint32_t i = results->count; i > insert_pos; i--) {
		results->results[i] = results->results[i - 1];
	}

	/* Insert new result */
	results->results[insert_pos] = new_result;
	results->count++;
} /* add_search_result */

/*------------------------------------------------------------------------*/
uint32_t active_set_search_enhanced(const TLV_ActiveSet *active,
				     const TLV_VariantIndex *vindex,
				     const char *search_text,
				     SearchMode mode,
				     SearchResultSet *results) {
	if (!active || !vindex || !search_text || !*search_text || !results) {
		return 0;
	}

	search_result_set_clear(results);

	/* Build dense list of active variants */
	TLV_ActiveSet *mutable_active = (TLV_ActiveSet*)active;
	const uint32_t *dense = active_set_build_dense(mutable_active);
	if (!dense) {
		return 0;
	}

	results->total_searched = active->active_count;

	/* Search within active variants */
	for (uint32_t i = 0; i < active->active_count; i++) {
		uint32_t variant_idx = dense[i];
		if (variant_idx >= vindex->count) continue;

		const TLV_VariantDescriptor *vd = &vindex->items[variant_idx];
		uint32_t relevance = calculate_relevance_score(vd, search_text, mode);

		if (relevance > 0) {
			/* Count matches in each field */
			uint32_t display_matches = 0;
			uint32_t filename_matches = 0; /* Currently not available */

			if (vd->display_name) {
				char lower_display[256];
				char lower_search[128];
				str_to_lower(lower_display, vd->display_name, sizeof(lower_display));
				str_to_lower(lower_search, search_text, sizeof(lower_search));
				display_matches = count_substring_matches(lower_display, lower_search);
			}

			/* Original filename not available in VariantDescriptor structure
			 * Future enhancement: access through TLV entries if needed */

			add_search_result(results, variant_idx, relevance,
					 display_matches, filename_matches);
		}
	}

	return results->count;
} /* active_set_search_enhanced */

/*------------------------------------------------------------------------*/
/* Parse multiple search terms and search for each */
uint32_t active_set_search_multi_term(const TLV_ActiveSet *active,
				       const TLV_VariantIndex *vindex,
				       const char *search_terms,
				       bool require_all_terms,
				       SearchResultSet *results) {
	if (!active || !vindex || !search_terms || !*search_terms || !results) {
		return 0;
	}

	/* Parse search terms (space-separated) */
	char terms_copy[256];
	strncpy(terms_copy, search_terms, sizeof(terms_copy) - 1);
	terms_copy[sizeof(terms_copy) - 1] = '\0';

	char *term_ptrs[16]; /* Support up to 16 terms */
	uint32_t term_count = 0;

	/* Split on spaces */
	char *token = strtok(terms_copy, " \t\n");
	while (token && term_count < 16) {
		term_ptrs[term_count++] = token;
		token = strtok(NULL, " \t\n");
	}

	if (term_count == 0) { return 0; }

	search_result_set_clear(results);

	/* Build dense list of active variants */
	TLV_ActiveSet *mutable_active = (TLV_ActiveSet*)active;
	const uint32_t *dense = active_set_build_dense(mutable_active);
	if (!dense) {
		return 0;
	}

	results->total_searched = active->active_count;

	/* Search within active variants */
	for (uint32_t i = 0; i < active->active_count; i++) {
		uint32_t variant_idx = dense[i];
		if (variant_idx >= vindex->count) continue;

		const TLV_VariantDescriptor *vd = &vindex->items[variant_idx];
		if (!vd->display_name) continue;

		/* Prepare lowercase versions */
		char lower_display[256] = {0};
		char lower_filename[256] = {0}; /* Not available but kept for future */
		if (vd->display_name) {
			str_to_lower(lower_display, vd->display_name, sizeof(lower_display));
		}
		/* Original filename not available in current structure */

		/* Check how many terms match */
		uint32_t matching_terms = 0;
		uint32_t total_display_matches = 0;
		uint32_t total_filename_matches = 0;
		uint32_t total_relevance = 0;

		for (uint32_t t = 0; t < term_count; t++) {
			char lower_term[64];
			str_to_lower(lower_term, term_ptrs[t], sizeof(lower_term));

			bool term_found = false;
			uint32_t term_display_matches = 0;
			uint32_t term_filename_matches = 0;

			if (lower_display[0] && strstr(lower_display, lower_term)) {
				term_found = true;
				term_display_matches = count_substring_matches(lower_display, lower_term);
				total_display_matches += term_display_matches;
			}

			/* Original filename search not available in current structure
			 * Future enhancement: access through TLV entries if needed */
			if (false) { /* Placeholder for filename search */
				term_found = true;
				term_filename_matches = count_substring_matches(lower_filename, lower_term);
				total_filename_matches += term_filename_matches;
			}

			if (term_found) {
				matching_terms++;
				/* Higher score for display name matches */
				total_relevance += term_display_matches * 100 + term_filename_matches * 25;
			}
		}

		/* Decide if this variant qualifies */
		bool qualifies = false;
		if (require_all_terms) {
			qualifies = (matching_terms == term_count);
		} else {
			qualifies = (matching_terms > 0);
		}

		if (qualifies && total_relevance > 0) {
			/* Bonus for matching more terms */
			uint32_t term_bonus = (matching_terms * 50);
			total_relevance += term_bonus;

			/* Bonus for matching all terms */
			if (matching_terms == term_count) {
				total_relevance += 200;
			}

			add_search_result(results, variant_idx, total_relevance,
					 total_display_matches, total_filename_matches);
		}
	}

	return results->count;
} /* active_set_search_multi_term */

/*------------------------------------------------------------------------*/
const SearchResult *search_result_set_get_top(const SearchResultSet *results,
					       uint32_t max_count,
					       uint32_t *out_count) {
	if (!results || !out_count) {
		if (out_count) *out_count = 0;
		return NULL;
	}

	uint32_t count = (results->count < max_count) ? results->count : max_count;
	*out_count = count;

	return (count > 0) ? results->results : NULL;
} /* search_result_set_get_top */

/*------------------------------------------------------------------------*/
/* End of Text */
