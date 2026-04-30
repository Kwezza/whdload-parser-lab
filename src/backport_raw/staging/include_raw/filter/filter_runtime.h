/* filter_runtime.h - Production filter runtime facade */
#ifndef FILTER_RUNTIME_H
#define FILTER_RUNTIME_H

#include <platform.h>
#include <stdint.h>
#include <stdbool.h>
#include <filter/filter_profile.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/csv_cache.h>
#include <filter/variant_index.h>

typedef struct {
	FieldRegistry *registry;       /* dynamic field registry */
	GlobalCSVManager csv_mgr;      /* CSV cache manager */
	FilterProfile profile;         /* active scoring profile */
	bool csv_initialized;          /* csv cache init flag */
	bool initialized;              /* overall init */
} FilterRuntime;

bool filter_runtime_init(FilterRuntime *rt, const char *defs_dir, const char *pack_types_ini_path);
bool filter_runtime_build_profile(FilterRuntime *rt,
		const char *chipset_include,
		const char *chipset_exclude,
		const char *language_include,
		const char *language_exclude,
		const char *memory_include,
		const char *memory_exclude,
		bool debug);
bool filter_runtime_load_snapshot(FilterRuntime *rt, const char *tlv_path, TLV_Record *out_rec, TLV_VariantIndex *out_index);
uint32_t filter_runtime_score_all(FilterRuntime *rt, const TLV_VariantIndex *vindex, uint32_t top_n);
void filter_runtime_shutdown(FilterRuntime *rt);

#endif /* FILTER_RUNTIME_H */
/* End of Text */
