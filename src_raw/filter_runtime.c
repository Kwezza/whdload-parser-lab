#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <filter/filter_runtime.h>
#include <filter/filter_pipeline.h>
#include <filter/profile_loader.h>
#include <tlv_filename/tlv_builder.h>
#include <utils/prefs.h>
#include <utils/logging.h>
#include <platform/platform_io.h>

/* Proper snapshot loader: uses tlv_read_record_with_metadata to honor embedded
 * field name -> id maps (TLV_TYPE_METADATA_MAP) produced during aggregation.
 * If a metadata map exists we do NOT currently swap the runtime registry; we
 * assume pack_types.ini generated identical mapping. For robustness we perform
 * a light validation of the display_name field id. */
static bool load_snapshot_record(const char *path, TLV_Record *rec, const FieldRegistry *rt_registry) {
	if (!path || !rec || !rt_registry) { return false; }
	FILE *f = whd_fopen(path, "rb");
	if (!f) { LOG_PRINTF("FILTER_RT: unable to open snapshot %s\n", path); return false; }
	FieldRegistry *file_registry = NULL;
	/* Read record; if snapshot supplies metadata map we capture reconstructed registry. */
	bool ok = tlv_read_record_with_metadata(f, rec, &file_registry);
	fclose(f);
	if (!ok) { LOG_PRINTF("FILTER_RT: tlv_read_record_with_metadata failed (%s)\n", path); return false; }
	/* If file supplied its own registry and it differs from runtime mapping, translate IDs. */
	if (file_registry) {
		uint32_t changes = 0; uint32_t unknown = 0;
		for (uint32_t i = 0; i < rec->entry_count; i++) {
			TLV_Entry *e = &rec->entries[i];
			const char *fname = field_registry_get_name(file_registry, e->field_id);
			if (!fname) { unknown++; continue; }
			uint8_t rid = field_registry_get_id(rt_registry, fname);
			if (rid != 0 && rid != e->field_id) { e->field_id = rid; changes++; }
		}
		LOG_PRINTF("FILTER_RT: snapshot field id remap: %u changed, %u unknown\n", (unsigned)changes, (unsigned)unknown);
		field_registry_free(file_registry);
	}
	/* Sanity check: first entry must be display_name boundary. */
	uint8_t disp_id = (uint8_t)field_registry_get_id(rt_registry, "display_name");
	if (rec->entry_count == 0 || rec->entries[0].field_id != disp_id) {
		LOG_PRINTF("FILTER_RT: snapshot first field_id=%u does not match display_name id=%u (format mismatch?)\n",
			 rec->entry_count?rec->entries[0].field_id:255, disp_id);
		return false;
	}
	return true;
}

bool filter_runtime_init(FilterRuntime *rt, const char *defs_dir, const char *pack_types_ini_path) {
	if (!rt) { return false; }
	memset(rt,0,sizeof(*rt));
	rt->registry = field_registry_alloc(); if (!rt->registry) { return false; }
	if (!build_field_registry_from_ini(rt->registry, pack_types_ini_path)) { LOG_PRINTF("FILTER_RT: registry build failed (%s)\n", pack_types_ini_path); return false; }
	if (!csv_cache_manager_init(&rt->csv_mgr, NULL, defs_dir)) { LOG_PRINTF("FILTER_RT: csv cache init failed (%s)\n", defs_dir); return false; }
	rt->csv_initialized = true;
	/* Pre-load CSVs for fields so we can capture default markers without waiting for lazy load */
	for (uint32_t i=0; i<field_registry_get_count(rt->registry); i++) {
		/* Use name->csv mapping */
		const char *fname = field_registry_get_name(rt->registry, (uint8_t)(FIELD_ID_DYNAMIC_MIN + i));
		(void)fname; /* placeholder: iterate by index below using registry->fields array not exposed publicly */
	}
	/* Iterate internal field array directly (structure known here) */
	/* Note: we rely on header guarantee that FieldDefinition layout has csv_filename */
	{
		/* cast away const for iteration of internal array */
		FieldRegistry *reg_mut = rt->registry;
		for (uint32_t fi=0; fi<reg_mut->field_count; fi++) {
			FieldDefinition *fd = &reg_mut->fields[fi];
			if (!fd->is_active) { continue; }
			if (fd->csv_filename[0] == '\0') { continue; }
			/* Ensure CSV loaded to read default marker */
			(void)csv_cache_load_file(&rt->csv_mgr, fd->csv_filename);
			bool has_def=false; uint32_t def_id = csv_cache_get_default_token(&rt->csv_mgr, fd->csv_filename, &has_def);
			if (has_def && def_id>0) { field_registry_set_default(rt->registry, fd->field_name, (uint16_t)(def_id & 0xFFFFu)); }
		}
	}
	/* Summary logging of defaults (concise) */
	{
		FieldRegistry *reg = rt->registry; unsigned defaults=0;
		for (uint32_t fi=0; fi<reg->field_count; fi++) {
			FieldDefinition *fd = &reg->fields[fi]; if (!fd->is_active || !fd->has_default) { continue; }
			LOG_PRINTF("FILTER_RT DEFAULT: field=%s token_id=%u\n", fd->field_name, (unsigned)fd->default_token_id);
			defaults++;
		}
		if (defaults==0) { LOG_PRINTF("FILTER_RT DEFAULT: none defined\n"); }
	}
	rt->initialized = true;
	return true;
}

static void apply_pref_if(const char *key, const char *val) { if (val && *val) { prefs_set_str(key, val); } }

bool filter_runtime_build_profile(FilterRuntime *rt,
		const char *chipset_inc,const char *chipset_exc,
		const char *language_inc,const char *language_exc,
		const char *memory_inc,const char *memory_exc,
		bool debug) {
	if (!rt || !rt->initialized) { return false; }
	/* Apply ad-hoc overrides (legacy path) */
	apply_pref_if("Filter.chipset.include", chipset_inc);
	apply_pref_if("Filter.chipset.exclude", chipset_exc);
	apply_pref_if("Filter.language.include", language_inc);
	apply_pref_if("Filter.language.exclude", language_exc);
	apply_pref_if("Filter.memory.include", memory_inc);
	apply_pref_if("Filter.memory.exclude", memory_exc);
	if (debug) { prefs_set_str("Filter.debug", "true"); }
	/* ActiveProfile indirection: prefs.ini may contain ActiveProfile key naming profile file (without path) */
	const char *active_prof = prefs_get_str("ActiveProfile");
	bool loaded=false;
	if (active_prof && *active_prof) {
		char file_component[192]; /* base name + optional extension */
		char path[256];
		/* Copy base safely */
		strncpy(file_component, active_prof, sizeof(file_component)-1); file_component[sizeof(file_component)-1]='\0';
		if (!strstr(file_component, ".profile")) {
			/* Append extension if space allows */
			size_t blen = strlen(file_component);
			if (blen + 8 < sizeof(file_component)) { strcat(file_component, ".profile"); }
		}
		int w = snprintf(path, sizeof(path), "assets/amiga_sys/profiles/%s", file_component);
		if (w > 0 && w < (int)sizeof(path)) {
			ProfileMeta meta; if (profile_load_file(path, &rt->profile, &meta, rt->registry, &rt->csv_mgr)) {
				loaded = true; LOG_PRINTF("FILTER_RT: loaded profile '%s' name='%s' format=%u version=%u warnings=%s\n", meta.id, meta.name, (unsigned)meta.profile_format, (unsigned)meta.version, meta.had_warnings?"yes":"no");
			} else {
				LOG_PRINTF("FILTER_RT: failed to load profile file %s (falling back to defaults)\n", path);
			}
		} else {
			LOG_PRINTF("FILTER_RT: profile path truncated/invalid for '%s'\n", active_prof);
		}
	}
	if (!loaded) { return filter_profile_load_defaults(&rt->profile, rt->registry, &rt->csv_mgr); }
	return true;
}

bool filter_runtime_load_snapshot(FilterRuntime *rt, const char *tlv_path, TLV_Record *out_rec, TLV_VariantIndex *out_index) {
	if (!rt || !rt->initialized || !out_rec || !out_index) { return false; }
	if (!load_snapshot_record(tlv_path, out_rec, rt->registry)) { return false; }
	if (!variant_index_build(out_rec, rt->registry, out_index)) { return false; }
	return true;
}

uint32_t filter_runtime_score_all(FilterRuntime *rt, const TLV_VariantIndex *vindex, uint32_t top_n) {
	if (!rt || !vindex) { return 0; }
	/* Build a temporary FilterPipeline using existing vindex (avoid rebuilding). */
	FilterPipeline fp; if (!filter_pipeline_init(&fp)) { return 0; }
	/* Inject existing index and counts directly (variant_index_build already done). */
	fp.vindex = *vindex; /* shallow copy; we do not own memory here */
	fp.variant_count = vindex->count;
	if (!active_set_init(&fp.active, fp.variant_count)) { filter_pipeline_free(&fp); return 0; }
	active_set_select_all(&fp.active);
	fp.scores = (uint32_t*)whd_malloc(sizeof(uint32_t)*fp.variant_count);
	if (!fp.scores) { filter_pipeline_free(&fp); return 0; }
	memset(fp.scores,0,sizeof(uint32_t)*fp.variant_count);
	/* Apply profile scoring */
	filter_pipeline_apply_profile(&fp, &rt->profile);
	/* Fetch sorted active indices */
	uint32_t sorted_count=0; const uint32_t *sorted = filter_pipeline_get_sorted(&fp, &sorted_count);
	if (!sorted) { filter_pipeline_free(&fp); return 0; }
	uint32_t emit = sorted_count;
	if (top_n && top_n < emit) { emit = top_n; }
	for (uint32_t i=0; i<emit; i++) {
		uint32_t vidx = sorted[i];
		uint32_t sc = fp.scores[vidx];
		LOG_PRINTF("FILTER_RT TOP[%u]: variant=%u score=%u tokens=%u\n", i, vidx, sc, vindex->items[vidx].token_count);
	}
	uint32_t total_active = sorted_count;
	/* Clean up pipeline allocations we own (but not vindex internal memory). */
	/* Prevent variant_index_free inside filter_pipeline_free from freeing shared memory: blank out pointer fields */
	fp.vindex.items = NULL; fp.vindex.count = 0; fp.vindex.capacity = 0;
	filter_pipeline_free(&fp);
	return total_active;
}

void filter_runtime_shutdown(FilterRuntime *rt) {
	if (!rt) { return; }
	if (rt->csv_initialized) { csv_cache_manager_cleanup(&rt->csv_mgr); }
	if (rt->registry) { field_registry_free(rt->registry); }
	memset(rt,0,sizeof(*rt));
}

/* End of Text */
