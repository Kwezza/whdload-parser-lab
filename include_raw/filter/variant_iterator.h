/* variant_iterator.h - Phase 1 TLV variant boundary iterator
 *
 * Provides lightweight iteration over logical variants inside a flat TLV_Record.
 * A variant is defined as a contiguous run of entries beginning with a
 * display_name field (dynamic field registry ID lookup) and continuing until
 * the next display_name or end of record.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-18
 */
#ifndef FILTER_VARIANT_ITERATOR_H
#define FILTER_VARIANT_ITERATOR_H

#include <platform.h>
#include <tlv_filename/tlv_builder.h>
#include <tlv_filename/field_registry.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Span describing one variant within the TLV_Record */
typedef struct {
	uint32_t start_entry;   /* index into TLV_Record.entries where variant starts */
	uint32_t entry_count;   /* number of entries in this variant (>=1) */
} TLV_VariantSpan;

/* Iterator state */
typedef struct {
	const TLV_Record *record;   /* source TLV record */
	uint32_t index;              /* current entry index cursor */
	uint8_t display_field_id;    /* cached field id for display_name */
	bool initialized;            /* init guard */
} TLV_VariantIter;

/* Initialize iterator. Returns false if record or display_name field missing */
bool tlv_variant_iter_init(TLV_VariantIter *it,
				 const TLV_Record *record,
				 const FieldRegistry *registry);

/* Advance to next variant. Returns true and fills span, false when no more. */
bool tlv_variant_iter_next(TLV_VariantIter *it, TLV_VariantSpan *out_span);

/* Validate record variant structure: first entry must be display_name and every
 * variant must start with display_name. Returns false if structural anomalies.
 */
bool tlv_record_validate_variants(const TLV_Record *record,
					 const FieldRegistry *registry,
					 uint32_t *out_variant_count);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_VARIANT_ITERATOR_H */
/* End of Text */
