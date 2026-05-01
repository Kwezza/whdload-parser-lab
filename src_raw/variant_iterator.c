#include <platform.h>
#include <filter/variant_iterator.h>

/*------------------------------------------------------------------------*/
/* Internal helper to fetch display_name field id (cached by caller) */
static bool get_display_field_id(const FieldRegistry *registry, uint8_t *out_id) {
	int id = field_registry_get_id(registry, "display_name");
	if(id < 0 || id > 0xFF) {
		return false; /* missing required field */
	}
	*out_id = (uint8_t)id;
	return true;
} /* get_display_field_id */
/*------------------------------------------------------------------------*/

bool tlv_variant_iter_init(TLV_VariantIter *it, const TLV_Record *record, const FieldRegistry *registry) {
	if(!it || !record || !registry) {
		return false;
	}
	memset(it, 0, sizeof(*it));
	it->record = record;
	if(!get_display_field_id(registry, &it->display_field_id)) {
		return false;
	}
	/* Structural pre-check: first entry must be display_name */
	if(record->entry_count == 0) {
		return false; /* empty record invalid for iteration */
	}
	if(record->entries[0].field_id != it->display_field_id) {
		return false; /* first entry must be variant boundary */
	}
	it->initialized = true;
	it->index = 0; /* next call to next() will consume first variant */
	return true;
} /* tlv_variant_iter_init */
/*------------------------------------------------------------------------*/

bool tlv_variant_iter_next(TLV_VariantIter *it, TLV_VariantSpan *out_span) {
	if(!it || !out_span || !it->initialized) {
		return false;
	}
	const TLV_Record *rec = it->record;
	if(it->index >= rec->entry_count) {
		return false; /* exhausted */
	}
	uint32_t start = it->index;
	uint32_t i = start + 1; /* scan forward until next boundary */
	for(; i < rec->entry_count; ++i) {
		if(rec->entries[i].field_id == it->display_field_id) {
			break; /* next variant begins */
		}
	}
	out_span->start_entry = start;
	out_span->entry_count = i - start;
	it->index = i; /* position at next variant start */
	return true;
} /* tlv_variant_iter_next */
/*------------------------------------------------------------------------*/

bool tlv_record_validate_variants(const TLV_Record *record, const FieldRegistry *registry, uint32_t *out_variant_count) {
	if(out_variant_count) {
		*out_variant_count = 0;
	}
	if(!record || !registry) {
		return false;
	}
	uint8_t display_id;
	if(!get_display_field_id(registry, &display_id)) {
		return false; /* registry missing display_name */
	}
	if(record->entry_count == 0) {
		return false; /* cannot be empty */
	}
	/* First entry must be boundary */
	if(record->entries[0].field_id != display_id) {
		return false;
	}
	/* Scan through entries ensuring no zero-length variants */
	uint32_t variants = 0;
	uint32_t i = 0;
	while(i < record->entry_count) {
		/* Each variant must start with display_name */
		if(record->entries[i].field_id != display_id) {
			return false;
		}
		uint32_t start = i;
		++i; /* skip boundary entry */
		for(; i < record->entry_count; ++i) {
			if(record->entries[i].field_id == display_id) {
				break; /* next variant */
			}
		}
		if(i - start == 0) { /* should not happen */
			return false;
		}
		variants++;
	}
	if(out_variant_count) {
		*out_variant_count = variants;
	}
	return true;
} /* tlv_record_validate_variants */
/*------------------------------------------------------------------------*/
/* End of Text */
