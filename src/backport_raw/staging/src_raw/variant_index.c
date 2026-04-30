#include <platform.h>
#include <filter/variant_index.h>
#include <string.h>
#include <utils/logging.h>
#include <tlv_filename/field_registry.h>

/* Simple FNV-1a 64-bit */
static uint64_t fnv1a64_lower(const char *s) {
	uint64_t h = 0xcbf29ce484222325ULL; /* offset */
	while (s && *s) {
		unsigned char c = (unsigned char)*s++;
		if (c >= 'A' && c <= 'Z') { c = (unsigned char)(c - 'A' + 'a'); }
		h ^= (uint64_t)c;
		h *= 0x100000001b3ULL;
	}
	return h;
} /* fnv1a64_lower */
/*------------------------------------------------------------------------*/

static bool ensure_capacity(TLV_VariantIndex *idx, uint32_t needed) {
	if (needed <= idx->capacity) { return true; }
	uint32_t new_cap = idx->capacity ? idx->capacity * 2 : 1024;
	if (new_cap < needed) { new_cap = needed; }
	TLV_VariantDescriptor *np = (TLV_VariantDescriptor*)whd_malloc(sizeof(TLV_VariantDescriptor)*new_cap);
	if (!np) { return false; }
	if (idx->items && idx->count) { memcpy(np, idx->items, sizeof(TLV_VariantDescriptor)*idx->count); }
	if (idx->items) { /* free old */
		for (uint32_t i = 0; i < idx->count; i++) {
			/* only shallow copy; display_name pointers transferred */
		}
		whd_free(idx->items);
	}
	idx->items = np;
	idx->capacity = new_cap;
	return true;
} /* ensure_capacity */
/*------------------------------------------------------------------------*/

bool variant_index_build(const TLV_Record *record,
			   const FieldRegistry *registry,
			   TLV_VariantIndex *out_index) {
	if (!record || !registry || !out_index) { return false; }
	memset(out_index, 0, sizeof(*out_index));
	out_index->min_fields = 0xFFFFFFFFu;
	TLV_VariantIter it;
	if (!tlv_variant_iter_init(&it, record, registry)) { return false; }
	TLV_VariantSpan span;
	/* Phase 1 coverage tracking for core filter fields. Extend list as new
	 * fields become filterable; logging emitted after build completes. */
	static const char *tracked_fields[] = { "chipset", "memory", "language", "video" };
	const size_t tracked_count = sizeof(tracked_fields)/sizeof(tracked_fields[0]);
	uint32_t variant_hits[4] = {0,0,0,0}; /* variants having >=1 token for field */
	uint32_t token_totals[4] = {0,0,0,0}; /* total tokens captured per field */
	uint8_t tracked_ids[4];
	bool tracked_multi[4];
	for (size_t tf = 0; tf < tracked_count; tf++) {
		tracked_ids[tf] = field_registry_get_id(registry, tracked_fields[tf]);
		tracked_multi[tf] = (tracked_ids[tf] != 0) && field_registry_get_allow_multiple(registry, tracked_fields[tf]);
	}
	/* Frequency counters for all field ids (0..255) excluding boundary display_name */
	uint32_t field_freq[256]; memset(field_freq, 0, sizeof(field_freq));
	uint8_t display_id = field_registry_get_id(registry, "display_name");
	uint32_t sample_variants_dumped = 0;
	while (tlv_variant_iter_next(&it, &span)) {
		if (!ensure_capacity(out_index, out_index->count + 1)) { variant_index_free(out_index); return false; }
		TLV_VariantDescriptor *vd = &out_index->items[out_index->count];
		memset(vd, 0, sizeof(*vd));
		vd->start_entry = span.start_entry;
		vd->entry_count = (uint16_t)span.entry_count;
		vd->interior_fields = (span.entry_count > 0) ? (uint16_t)(span.entry_count - 1) : 0;
		vd->token_count = 0;
		bool field_seen[4] = { false,false,false,false }; /* per-variant flags */
		for (uint32_t e = 0; e < vd->entry_count && vd->token_count < VI_MAX_TOKENS; e++) {
			const TLV_Entry *fe = &record->entries[vd->start_entry + e];
			uint8_t fid = fe->field_id; if (fe->length == 0) { continue; }
			if (fid != display_id) { field_freq[fid]++; }
			int tracked_index = -1; bool allow_multi=false;
			for (size_t tf=0; tf<tracked_count; tf++) {
				if (fid == tracked_ids[tf] && fid != 0) { tracked_index=(int)tf; allow_multi = tracked_multi[tf]; break; }
			}
			if (tracked_index < 0) { continue; }
			if (!allow_multi) {
				bool exists=false; for (uint8_t k=0;k<vd->token_count;k++){ if (vd->tokens[k].field_id==fid){ exists=true; break; } }
				if (exists) { continue; }
			}
			if (vd->token_count < VI_MAX_TOKENS) {
				vd->tokens[vd->token_count].field_id = fid;
				vd->tokens[vd->token_count].value = (const char*)fe->value;
				vd->tokens[vd->token_count].length = fe->length;
				if (fe->length == 4 && fe->value) {
					uint32_t id_tmp = 0; memcpy(&id_tmp, fe->value, 4);
					vd->tokens[vd->token_count].csv_id = id_tmp;
				} else {
					vd->tokens[vd->token_count].csv_id = 0;
				}
				vd->token_count++;
				/* Coverage accounting */
				if (tracked_index >=0 && tracked_index < (int)tracked_count) {
					token_totals[tracked_index]++;
					if (!field_seen[tracked_index]) { field_seen[tracked_index] = true; }
				}
			}
		}
		/* Inject synthetic default tokens for any tracked field not present so that
		 * variants lacking an explicit token (e.g., base OCS chipset) still carry
		 * a token id for scoring/pairing. We only add if no token for that field
		 * was seen in the variant and a registry default is defined. */
		for (size_t tf=0; tf<tracked_count; tf++) {
			if (!tracked_ids[tf]) { continue; }
			if (field_seen[tf]) { continue; } /* already has at least one token */
			bool has_def=false; uint16_t def_tok = field_registry_get_default_token_id(registry, tracked_ids[tf], &has_def);
			if (!has_def || !def_tok) { continue; }
			if (vd->token_count < VI_MAX_TOKENS) {
				vd->tokens[vd->token_count].field_id = tracked_ids[tf];
				vd->tokens[vd->token_count].value = NULL; /* synthetic */
				vd->tokens[vd->token_count].length = 0;
				vd->tokens[vd->token_count].csv_id = def_tok;
				vd->token_count++;
				field_seen[tf] = true; /* count as coverage */
				/* Do not increment field_freq: not an explicit TLV entry */
			}
		}
		/* Update per-field variant hit counters after processing variant */
		for (size_t tf=0; tf<tracked_count; tf++) { if (field_seen[tf]) { variant_hits[tf]++; } }
		/* Sample dump for first few variants to inspect raw entries (token extraction diagnostics) */
		if (sample_variants_dumped < 5) {
			LOG_PRINTF("VINDEX SAMPLE VARIANT %u: start=%u entries=%u tokens=%u name='%s'\n", out_index->count, vd->start_entry, vd->entry_count, vd->token_count, vd->display_name?vd->display_name:"?");
			for (uint32_t e = 0; e < vd->entry_count && e < 12; e++) {
				const TLV_Entry *fe = &record->entries[vd->start_entry + e];
				const char *fname = field_registry_get_name(registry, fe->field_id);
				uint32_t preview_len = fe->length < 8 ? fe->length : 8; /* up to 8 bytes preview */
				char hexbuf[3*8+1]; hexbuf[0]='\0';
				for (uint32_t hb=0; hb<preview_len; hb++) { char tmp[4]; snprintf(tmp,sizeof(tmp),"%02X", ((const unsigned char*)fe->value)[hb]); strncat(hexbuf,tmp,sizeof(hexbuf)-strlen(hexbuf)-1); if (hb+1<preview_len){ strncat(hexbuf,"",sizeof(hexbuf)-strlen(hexbuf)-1);} }
				LOG_PRINTF("VINDEX SAMPLE ENTRY: v=%u e=%u fid=%u name=%s len=%u data=%s\n", out_index->count, e, fe->field_id, fname?fname:"?", fe->length, hexbuf);
			}
			sample_variants_dumped++;
		}
		const TLV_Entry *first = &record->entries[span.start_entry];
		if (first->length > 0 && first->value) {
			uint32_t copy = first->length;
			if (copy > 1023) { copy = 1023; }
			vd->display_name = (char*)whd_malloc(copy + 1);
			if (!vd->display_name) { variant_index_free(out_index); return false; }
			memcpy(vd->display_name, first->value, copy);
			vd->display_name[copy] = '\0';
			vd->display_len = copy;
			vd->name_hash = fnv1a64_lower(vd->display_name);
			/* Derive base_name: per golden rule it's everything before the first underscore */
			vd->base_name[0] = '\0';
			if (copy > 0) {
				size_t bn = copy; if (bn >= sizeof(vd->base_name)) { bn = sizeof(vd->base_name)-1; }
				memcpy(vd->base_name, vd->display_name, bn); vd->base_name[bn]='\0';
				char *first_us = strchr(vd->base_name, '_');
				if (first_us) { *first_us='\0'; }
				vd->base_hash = fnv1a64_lower(vd->base_name);
			}
		}
		/* Update aggregate stats */
		if (vd->interior_fields < out_index->min_fields) { out_index->min_fields = vd->interior_fields; }
		if (vd->interior_fields > out_index->max_fields) { out_index->max_fields = vd->interior_fields; }
		out_index->count++;
	}
	if (out_index->count > 0) {
		uint64_t sum = 0;
		for (uint32_t i = 0; i < out_index->count; i++) { sum += out_index->items[i].interior_fields; }
#if PLATFORM_AMIGA
		out_index->avg_fields_x100 = (uint32_t)((sum * 100) / out_index->count);
#else
		out_index->avg_fields = (double)sum / (double)out_index->count;
#endif
		if (out_index->min_fields == 0xFFFFFFFFu) { out_index->min_fields = 0; }
	}
	/* Emit single field frequency summary after processing all variants */
	LOG_PRINTF("VINDEX FIELD FREQ SUMMARY:\n");
	for (uint32_t fid=0; fid<256; fid++) {
		if (field_freq[fid] == 0) { continue; }
		const char *fname = field_registry_get_name(registry, (uint8_t)fid);
		LOG_PRINTF("VINDEX FIELD FREQ: fid=%u name=%s count=%u\n", fid, fname?fname:"?", (unsigned)field_freq[fid]);
	}
	/* Emit coverage logging once (Phase 1) */
		for (size_t tf=0; tf<tracked_count; tf++) {
			if (!tracked_ids[tf]) { continue; }
			uint32_t vh = variant_hits[tf]; uint32_t tv = out_index->count ? out_index->count : 1;
#if PLATFORM_AMIGA
			uint32_t pct_x10 = (1000 * vh) / tv; /* times 10 for one decimal place */
			LOG_PRINTF("VINDEX TOKENS: field=%s id=%u variants=%u/%u (%u.%u%%) tokens=%u\n",
				tracked_fields[tf], tracked_ids[tf], (unsigned)vh, (unsigned)out_index->count, pct_x10 / 10, pct_x10 % 10, (unsigned)token_totals[tf]);
#else
			double pct = (100.0 * (double)vh) / (double)tv;
			LOG_PRINTF("VINDEX TOKENS: field=%s id=%u variants=%u/%u (%.1f%%) tokens=%u\n",
				tracked_fields[tf], tracked_ids[tf], (unsigned)vh, (unsigned)out_index->count, pct, (unsigned)token_totals[tf]);
#endif
		}
	return true;
} /* variant_index_build */
/*------------------------------------------------------------------------*/

void variant_index_free(TLV_VariantIndex *index) {
	if (!index) { return; }
	if (index->items) {
		for (uint32_t i = 0; i < index->count; i++) {
			if (index->items[i].display_name) { whd_free(index->items[i].display_name); }
		}
		whd_free(index->items);
	}
	memset(index, 0, sizeof(*index));
} /* variant_index_free */
/*------------------------------------------------------------------------*/
/* End of Text */
