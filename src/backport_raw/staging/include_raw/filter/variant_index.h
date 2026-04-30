/* variant_index.h - Phase 2 Variant Index construction
 *
 * Builds an in-memory index (array) of normalized TLV_VariantDescriptor
 * structures from a flat TLV_Record. Each descriptor exposes:
 *  - start_entry / entry_count (boundary aware)
 *  - interior field count (excluding display_name boundary)
 *  - display_name (NUL-terminated copy for safe usage/search)
 *  - cached hash (64-bit FNV-1a of display_name lowercase) for fast lookup
 *  - offset into TLV entries enabling later expansion (ActiveSet, scoring)
 *
 * Author: GitHub Copilot
 * Created: 2025-08-18
 */
#ifndef FILTER_VARIANT_INDEX_H
#define FILTER_VARIANT_INDEX_H

#include <platform.h>
#include <filter/variant_iterator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptor for one variant */
/* NOTE: Phase 4 multi-value support: we now capture multiple occurrences of
 * allow-multiple fields (e.g. language, variant_tags) instead of only the
 * first token. The fixed upper bound keeps structure POD for fast indexing.
 */
/* Maximum captured tokens per variant (across all tracked fields). Chosen
 * conservatively; adjust if profiling shows legitimate truncation. */
/* Allow up to 16 captured tokens per variant (aggregate across tracked fields). */
#define VI_MAX_TOKENS 16
typedef struct {
	uint32_t start_entry;      /* index into TLV_Record.entries */
	uint16_t entry_count;      /* total entries (>=1, includes boundary) */
	uint16_t interior_fields;  /* entry_count - 1 (may be 0) */
	char *display_name;        /* owned NUL-terminated copy */
	uint32_t display_len;      /* length excluding NUL */
	uint64_t name_hash;        /* lowercase FNV-1a hash for quick matching */
	char base_name[128];       /* normalized name without version/build suffixes for pairing */
	uint64_t base_hash;        /* hash of base_name lowercase */
	/* Captured field tokens (single or multi-value). For fields that do not
	 * allow multiple, only the first occurrence is stored. For allow-multiple
	 * fields, each occurrence is appended until capacity reached. */
	struct {
		uint8_t field_id;      /* field id */
		const char *value;     /* pointer into TLV entry value (not owned) */
		uint16_t length;       /* value length (bytes) */
		uint32_t csv_id;       /* resolved CSV numeric ID if applicable (0 if none) */
	} tokens[VI_MAX_TOKENS];
	uint8_t token_count;      /* number of filled token slots */
} TLV_VariantDescriptor;

/* Index of all variants */
typedef struct {
	TLV_VariantDescriptor *items; /* array */
	uint32_t count;               /* number of variants */
	uint32_t capacity;            /* allocated descriptor slots */
	/* Aggregate stats (interior field counts) */
	uint32_t min_fields;
	uint32_t max_fields;
#if PLATFORM_AMIGA
	uint32_t avg_fields_x100;  /* Average * 100 to avoid floating point on Amiga */
#else
	double avg_fields;
#endif
} TLV_VariantIndex;

/* Build full variant index from TLV_Record. Returns false on structural error or OOM. */
bool variant_index_build(const TLV_Record *record,
			   const FieldRegistry *registry,
			   TLV_VariantIndex *out_index);

/* Free resources owned by variant index */
void variant_index_free(TLV_VariantIndex *index);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_VARIANT_INDEX_H */
/* End of Text */
