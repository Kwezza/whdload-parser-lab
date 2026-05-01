#include <platform.h>
#include <filter/filter_profile.h>
#include <string.h>
#include <ctype.h>
#include <utils/prefs.h>
#include <utils/logging.h>
#include <tlv_filename/field_registry.h>
#include <utils/pref_flags.h>

void filter_profile_init(FilterProfile *pf) {
	if (!pf) { return; }
	memset(pf, 0, sizeof(*pf));
	pf->debug_enabled = false;
	pf->reg = NULL;
	pf->csv_mgr = NULL;
} /* filter_profile_init */
/*------------------------------------------------------------------------*/

/* Simple comma splitter (ASCII) */
static uint8_t split_tokens(char *buf, char *out[][2], uint8_t max_tokens) {
	uint8_t count = 0; char *p = buf;
	while (*p && count < max_tokens) {
		while (*p == ' ' || *p == '\t' || *p == ',') { p++; }
		if (!*p) { break; }
		char *start = p;
		while (*p && *p != ',') { p++; }
		if (*p == ',') { *p++ = '\0'; }
		out[count][0] = start; out[count][1] = NULL; /* placeholder second column future */
		count++;
	}
	return count;
}

/* Retrieve preference string directly from prefs system */
static const char *get_pref(const char *key) {
	return prefs_get_str(key); /* returns NULL if not present */
}

/* Compute stable lowercase FNV-1a 8-bit hash for token (provisional real mapping).
 * This replaces the earlier first-character synthetic id so that distinct
 * tokens like AGA / ECS / OCS no longer collide if they share initial letter.
 * Future enhancement: integrate with CSV registry to obtain canonical numeric
 * IDs if available. */
static unsigned char token_hash8(const char *tok) {
	if (!tok) { return 0; }
	uint32_t h = 2166136261u; /* 32-bit FNV-1a basis */
	for (const unsigned char *p=(const unsigned char*)tok; *p; ++p) {
		unsigned char c = *p;
		if (c >= 'A' && c <= 'Z') { c = (unsigned char)(c - 'A' + 'a'); }
		h ^= c;
		h *= 16777619u; /* FNV prime */
	}
	/* Final fold down to 8 bits (XOR fold improves dispersion slightly) */
	h ^= (h >> 16);
	h ^= (h >> 8);
	return (unsigned char)(h & 0xFFu);
}

/* Build default or preference-driven profile */
bool filter_profile_load_defaults(FilterProfile *pf, const FieldRegistry *reg, GlobalCSVManager *csv_mgr) {
	if (!pf || !reg) { return false; }
	filter_profile_init(pf);
	pf->reg = reg;
	pf->csv_mgr = csv_mgr;
	/* Enable debug logging if preference key Filter.debug=true */
	/* Unified via prefs_flag_enabled helper */
	pf->debug_enabled = prefs_flag_enabled("Filter.debug", false);
	if (pf->debug_enabled) { LOG_PRINTF("FP SUMMARY: debug preference active (Filter.debug=enabled)\n"); }
	const struct { const char *fname; uint8_t weight; } base[] = {
		{ "chipset", 150 }, { "language", 120 }, { "memory", 100 }
	};
	for (size_t i=0;i<sizeof(base)/sizeof(base[0]) && pf->field_count < FP_MAX_FIELDS;i++) {
		uint8_t fid = field_registry_get_id(reg, base[i].fname);
		if (!fid) { continue; }
		FP_FieldProfile *fp = &pf->fields[pf->field_count++];
		memset(fp,0,sizeof(*fp));
		fp->field_id = fid; fp->weight = base[i].weight; fp->in_use = true; fp->allow_multiple = field_registry_get_allow_multiple(reg, base[i].fname);
		for (size_t r=0;r<sizeof(fp->rank_by_id);r++){ fp->rank_by_id[r]=0xFF; }
		/* Attempt to load include / exclude lists: keys Filter.<field>.include / Filter.<field>.exclude */
		char key[64]; const char *raw;
			snprintf(key,sizeof(key),"Filter.%s.include", base[i].fname); raw = get_pref(key);
		if (raw && *raw) {
			char tmp[256]; strncpy(tmp, raw, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
			char *slots[FP_MAX_INCLUDE][2]; uint8_t ct = split_tokens(tmp, slots, FP_MAX_INCLUDE);
			for (uint8_t t=0;t<ct;t++){
					unsigned char sid_hash = token_hash8(slots[t][0]);
					uint32_t resolved_id = 0;
					/* Attempt real CSV lookup using field csv base if available */
					if (csv_mgr) {
						const char *csv_base = get_csv_filename_for_field(reg, base[i].fname);
						char csv_name_only[64]; csv_name_only[0]='\0';
						if (csv_base) {
							size_t len = strlen(csv_base);
							if (len >= 4 && strcmp(&csv_base[len-4], ".csv") == 0) {
								size_t nlen = len-4; if (nlen >= sizeof(csv_name_only)) { nlen = sizeof(csv_name_only)-1; }
								memcpy(csv_name_only, csv_base, nlen); csv_name_only[nlen]='\0';
							} else {
								strncpy(csv_name_only, csv_base, sizeof(csv_name_only)-1); csv_name_only[sizeof(csv_name_only)-1]='\0';
							}
						} else {
							strncpy(csv_name_only, base[i].fname, sizeof(csv_name_only)-1); csv_name_only[sizeof(csv_name_only)-1]='\0';
						}
						if (csv_name_only[0]) { resolved_id = csv_cache_lookup(csv_mgr, csv_name_only, slots[t][0]); }
					}
					uint16_t store_id = resolved_id ? (uint16_t)(resolved_id & 0xFFFFu) : (uint16_t)sid_hash; /* fallback to hash */
					fp->include_ids[t] = store_id;
					fp->rank_by_id[store_id & 0xFF] = t; /* rank table indexed by low byte */
					if (pf->debug_enabled) { LOG_PRINTF("FP DBG: field=%s include[%u]='%s' csv_id=%u hash=0x%02X stored=0x%04X rank=%u\n", base[i].fname, t, slots[t][0], (unsigned)resolved_id, sid_hash, store_id, t); }
			}
			fp->include_count = ct;
		}
		else if (pf->debug_enabled) {
			LOG_PRINTF("FP INFO: field=%s has no include list\n", base[i].fname);
		}
			snprintf(key,sizeof(key),"Filter.%s.exclude", base[i].fname); raw = get_pref(key);
		if (raw && *raw) {
			char tmp[256]; strncpy(tmp, raw, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
			char *slots[FP_MAX_EXCLUDE][2]; uint8_t ct = split_tokens(tmp, slots, FP_MAX_EXCLUDE);
				for (uint8_t t=0;t<ct;t++){
					unsigned char sid_hash = token_hash8(slots[t][0]);
					uint32_t resolved_id = 0;
					if (csv_mgr) {
						const char *csv_base = get_csv_filename_for_field(reg, base[i].fname);
						char csv_name_only[64]; csv_name_only[0]='\0';
						if (csv_base) {
							size_t len = strlen(csv_base);
							if (len >= 4 && strcmp(&csv_base[len-4], ".csv") == 0) {
								size_t nlen = len-4; if (nlen >= sizeof(csv_name_only)) { nlen = sizeof(csv_name_only)-1; }
								memcpy(csv_name_only, csv_base, nlen); csv_name_only[nlen]='\0';
							} else {
								strncpy(csv_name_only, csv_base, sizeof(csv_name_only)-1); csv_name_only[sizeof(csv_name_only)-1]='\0';
							}
						} else {
							strncpy(csv_name_only, base[i].fname, sizeof(csv_name_only)-1); csv_name_only[sizeof(csv_name_only)-1]='\0';
						}
						if (csv_name_only[0]) { resolved_id = csv_cache_lookup(csv_mgr, csv_name_only, slots[t][0]); }
					}
					uint16_t store_id = resolved_id ? (uint16_t)(resolved_id & 0xFFFFu) : (uint16_t)sid_hash;
					fp->exclude_ids[t] = store_id;
					if (pf->debug_enabled) { LOG_PRINTF("FP DBG: field=%s exclude[%u]='%s' csv_id=%u hash=0x%02X stored=0x%04X\n", base[i].fname, t, slots[t][0], (unsigned)resolved_id, sid_hash, store_id); }
			}
			fp->exclude_count = ct;
		}
		if (pf->debug_enabled) {
			LOG_PRINTF("FP SUMMARY: field=%s id=%u weight=%u include=%u exclude=%u allow_multiple=%s\n",
				base[i].fname, fid, fp->weight, fp->include_count, fp->exclude_count,
				fp->allow_multiple?"yes":"no");
		}
	}
	if (pf->debug_enabled) { LOG_PRINTF("FP SUMMARY: total_fields=%u\n", pf->field_count); }
	return true;
} /* filter_profile_load_defaults */
/*------------------------------------------------------------------------*/

	/* Scoring: multi-value aware (best-of rank among tokens captured per field) plus interior field heuristic bonus */
uint32_t filter_score_variant_detailed(const FilterProfile *pf,
				const TLV_VariantIndex *vindex,
				uint32_t variant_index,
				bool *rejected,
				FP_ScoreDetail *detail) {
	if (rejected) { *rejected = false; }
	if (detail) { for (uint8_t i=0;i<FP_MAX_FIELDS;i++){ detail->field_ranks[i]=0xFF; detail->field_tokens[i]=0; } }
	if (!pf || !vindex || variant_index >= vindex->count) { if (rejected) { *rejected = true; } return 0; }
	const TLV_VariantDescriptor *vd = &vindex->items[variant_index];
	uint32_t total = 0;
	for (uint8_t i=0;i<pf->field_count;i++) {
		const FP_FieldProfile *fp = &pf->fields[i]; if (!fp->in_use || fp->weight==0) { continue; }
		int best_rank = -1; uint16_t best_eff_id=0; bool any_excluded=false; bool matched=false;
		for (uint8_t p=0; p<vd->token_count; p++) {
			if (vd->tokens[p].field_id != fp->field_id) { continue; }
			matched = true; const char *val = vd->tokens[p].value; uint32_t csv_id = vd->tokens[p].csv_id; unsigned char hash_id=0;
			if (!csv_id) { hash_id = token_hash8(val); }
			uint16_t eff_id = csv_id ? (uint16_t)(csv_id & 0xFFFFu) : (uint16_t)hash_id;
			for (uint8_t ex=0; ex<fp->exclude_count; ex++) { if (fp->exclude_ids[ex] == eff_id) { any_excluded=true; break; } }
			if (any_excluded) { break; }
			uint8_t rank = fp->rank_by_id[eff_id & 0xFF];
			if (rank != 0xFF && (best_rank==-1 || rank < best_rank)) { best_rank = rank; best_eff_id = eff_id; }
		}
		if (any_excluded) { if (rejected) { *rejected = true; } if (pf->debug_enabled){ LOG_PRINTF("FP MISS: variant=%u field_id=%u excluded-by-token\n", variant_index, fp->field_id); } return 0; }
		if (best_rank < 0) {
			bool has_def=false; uint16_t def_tok = field_registry_get_default_token_id(pf->reg, fp->field_id, &has_def);
			if (has_def && def_tok) {
				for (uint8_t ex=0; ex<fp->exclude_count; ex++) { if (fp->exclude_ids[ex] == def_tok) { any_excluded=true; break; } }
				if (any_excluded) { if (rejected){ *rejected=true; } if (pf->debug_enabled){ LOG_PRINTF("FP MISS: variant=%u field_id=%u excluded-by-default\n", variant_index, fp->field_id); } return 0; }
				uint8_t rank = fp->rank_by_id[def_tok & 0xFF];
				if (rank != 0xFF) { best_rank = rank; best_eff_id = def_tok; }
			}
		}
		if (best_rank >= 0) {
			uint32_t field_score = (uint32_t)(fp->include_count - (uint32_t)best_rank);
			if (field_score == 0) { field_score = 1; }
			total += field_score * fp->weight;
			if (detail) { detail->field_ranks[i] = (uint8_t)best_rank; detail->field_tokens[i] = best_eff_id; }
			if (pf->debug_enabled) { LOG_PRINTF("FP HIT: variant=%u field_id=%u rank=%d add=%u eff=0x%04X\n", variant_index, fp->field_id, best_rank, field_score * fp->weight, best_eff_id); }
		} else if (pf->debug_enabled) {
			LOG_PRINTF("FP MISS: variant=%u field_id=%u (no matching tokens%s)\n", variant_index, fp->field_id, matched?" (unranked)":"");
		}
	}
	if (vd->interior_fields) { total += vd->interior_fields; if (pf->debug_enabled) { LOG_PRINTF("FP BONUS: variant=%u interior_fields=%u added=%u\n", variant_index, vd->interior_fields, vd->interior_fields); } }
	return total;
}

uint32_t filter_score_variant(const FilterProfile *pf,
				const TLV_VariantIndex *vindex,
				uint32_t variant_index,
				bool *rejected) {
	return filter_score_variant_detailed(pf, vindex, variant_index, rejected, NULL);
} /* filter_score_variant */
/*------------------------------------------------------------------------*/
/* End of Text */
