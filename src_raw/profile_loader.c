/* profile_loader.c - full profile parser implementation
 * Supports sections:
 *   [Profile] id= name= version= profile_format= debug=1
 *   [Filter.<field>] include=CSV,LIST exclude=CSV,LIST
 *   [Scoring] weight.<field>=N
 * Falls back to filter_profile_load_defaults if no filter sections parsed.
 */
#include <platform.h>
#include <filter/profile_loader.h>
#include <utils/logging.h>
#include <utils/prefs.h>
#include <utils/pref_flags.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Local helpers (duplicated minimal logic to avoid exposing internals from filter_profile.c) */
static unsigned char token_hash8(const char *tok) {
	if (!tok) { return 0; }
	uint32_t h = 2166136261u;
	for (const unsigned char *p=(const unsigned char*)tok; *p; ++p) {
		unsigned char c = *p;
		if (c >= 'A' && c <= 'Z') { c = (unsigned char)(c - 'A' + 'a'); }
		h ^= c; h *= 16777619u; /* FNV-1a */
	}
	h ^= (h >> 16); h ^= (h >> 8);
	return (unsigned char)(h & 0xFFu);
}

static void trim(char *s) {
	if (!s) { return; }
	char *p = s; while (*p && (*p==' '||*p=='\t' || *p=='\r')) { p++; }
	if (p!=s) { memmove(s,p,strlen(p)+1); }
	char *e = s + strlen(s); while (e>s && (e[-1]==' '||e[-1]=='\t' || e[-1]=='\r')) { *--e='\0'; }
}

static uint8_t split_csv_list(char *buf, char *out[], uint8_t max_tokens) {
	uint8_t c=0; char *p=buf;
	while (*p && c<max_tokens) {
		while (*p==','||*p==' '||*p=='\t') { p++; }
		if (!*p) { break; }
		out[c++] = p;
		while (*p && *p!=',') { p++; }
		if (*p==',') { *p++='\0'; }
	}
	return c;
}

static FP_FieldProfile *ensure_field(FilterProfile *pf, uint8_t fid, const FieldRegistry *reg) {
	for (uint8_t i=0;i<pf->field_count;i++) { if (pf->fields[i].field_id==fid) { return &pf->fields[i]; } }
	if (pf->field_count >= FP_MAX_FIELDS) { return NULL; }
	FP_FieldProfile *fp = &pf->fields[pf->field_count++];
	memset(fp,0,sizeof(*fp));
	for (size_t r=0;r<sizeof(fp->rank_by_id);r++){ fp->rank_by_id[r]=0xFF; }
	fp->field_id = fid;
	fp->in_use = true;
	fp->allow_multiple = field_registry_get_allow_multiple(reg, field_registry_get_name(reg, fid));
	/* default weight left 0 until scoring section or defaults applied */
	return fp;
}

static void apply_list(FilterProfile *pf, FP_FieldProfile *fp, const FieldRegistry *reg, GlobalCSVManager *csv_mgr, const char *field_name, char *val, bool is_include, ProfileMeta *meta, bool debug_enabled) {
	/* pf currently not needed beyond potential future global settings; touch to silence unused warning */
	(void)pf;
	if (!val || !*val) { return; }
	char *tokens[FP_MAX_INCLUDE]; /* same size for exclude; we'll bound later */
	uint8_t max_tok = is_include ? FP_MAX_INCLUDE : FP_MAX_EXCLUDE;
	uint8_t ct = split_csv_list(val, tokens, max_tok);
	for (uint8_t i=0;i<ct;i++) {
		trim(tokens[i]); if (!*tokens[i]) { continue; }
		uint32_t resolved_id = 0; unsigned char h = token_hash8(tokens[i]);
		if (csv_mgr) {
			const char *csv_base = get_csv_filename_for_field(reg, field_name);
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
				strncpy(csv_name_only, field_name, sizeof(csv_name_only)-1); csv_name_only[sizeof(csv_name_only)-1]='\0';
			}
			if (csv_name_only[0]) { resolved_id = csv_cache_lookup(csv_mgr, csv_name_only, tokens[i]); }
		}
		uint16_t store_id = resolved_id ? (uint16_t)(resolved_id & 0xFFFFu) : (uint16_t)h;
		if (is_include) {
			if (fp->include_count < FP_MAX_INCLUDE) {
				fp->include_ids[fp->include_count] = store_id;
				fp->rank_by_id[store_id & 0xFF] = fp->include_count; /* rank is insertion order */
				if (debug_enabled) { LOG_PRINTF("FP DBG: field=%s include[%u]='%s' id=0x%04X csv=%u\n", field_name, fp->include_count, tokens[i], store_id, (unsigned)resolved_id); }
				fp->include_count++;
			} else { if (meta) { meta->had_warnings = true; } }
		} else {
			if (fp->exclude_count < FP_MAX_EXCLUDE) {
				fp->exclude_ids[fp->exclude_count] = store_id;
				if (debug_enabled) { LOG_PRINTF("FP DBG: field=%s exclude[%u]='%s' id=0x%04X csv=%u\n", field_name, fp->exclude_count, tokens[i], store_id, (unsigned)resolved_id); }
				fp->exclude_count++;
			} else { if (meta) { meta->had_warnings = true; } }
		}
	}
}

bool profile_load_file(const char *path, FilterProfile *out, ProfileMeta *meta, const FieldRegistry *reg, GlobalCSVManager *csv_mgr) {
	if (meta) { memset(meta,0,sizeof(*meta)); }
	if (!path || !out || !reg) { return false; }
	FILE *fp = whd_fopen(path, "rb"); if (!fp) { return false; }
	filter_profile_init(out); out->reg = reg; out->csv_mgr = csv_mgr;
	char line[512]; char section[64] = {0};
	bool file_debug=false; /* debug flag from profile */
	while (fgets(line,sizeof(line),fp)) {
		char *p = line; trim(p);
		if (!*p || *p=='#' || *p==';') { continue; }
		if (*p=='[') {
			char *r = strchr(p,']'); if (r) { *r='\0'; const char *src = p+1; size_t si=0; while (src[si] && si < sizeof(section)-1) { section[si]=src[si]; si++; } section[si]='\0'; }
			continue;
		}
		char *eq = strchr(p,'='); if (!eq) { continue; }
		*eq='\0'; char *key=p; char *val=eq+1; trim(key); trim(val);
		if (strcmp(section,"Profile")==0) {
			if (meta) {
				if (strcmp(key,"id")==0) { strncpy(meta->id,val,sizeof(meta->id)-1); }
				else if (strcmp(key,"name")==0) { strncpy(meta->name,val,sizeof(meta->name)-1); }
				else if (strcmp(key,"version")==0) { meta->version = (uint32_t)atoi(val); }
				else if (strcmp(key,"profile_format")==0) { meta->profile_format = (uint32_t)atoi(val); }
			}
			if (strcmp(key,"debug")==0) { if (*val=='1' || *val=='y' || *val=='Y' || *val=='t' || *val=='T') { file_debug=true; } }
			continue;
		}
		if (strncmp(section,"Filter.",7)==0) {
			const char *field_name = section+7;
			uint8_t fid = field_registry_get_id(reg, field_name); if (!fid) { if (meta) { meta->had_warnings=true; } continue; }
			FP_FieldProfile *fpf = ensure_field(out, fid, reg); if (!fpf) { if (meta) { meta->had_warnings=true; } continue; }
			if (strcmp(key,"include")==0) {
				apply_list(out, fpf, reg, csv_mgr, field_name, val, true, meta, file_debug);
			} else if (strcmp(key,"exclude")==0) {
				apply_list(out, fpf, reg, csv_mgr, field_name, val, false, meta, file_debug);
			}
			continue;
		}
		if (strcmp(section,"Scoring")==0) {
			if (strncmp(key,"weight.",7)==0) {
				const char *fname = key+7;
				uint8_t fid = field_registry_get_id(reg, fname); if (!fid) { if (meta){meta->had_warnings=true;} continue; }
				FP_FieldProfile *fpf = ensure_field(out, fid, reg); if (!fpf) { if (meta){meta->had_warnings=true;} continue; }
				int w = atoi(val); if (w<0) w=0; if (w>255) w=255; fpf->weight = (uint8_t)w;
				if (file_debug) { LOG_PRINTF("FP DBG: weight field=%s id=%u weight=%u\n", fname, fid, fpf->weight); }
			}
			continue;
		}
	}
	whd_fclose(fp);
	/* Post-parse normalization: trim any stray CR/LF from meta id/name (profiles sometimes ship with trailing newline artifacts) */
	if (meta) {
		trim(meta->id);
		trim(meta->name);
		/* Also remove any internal CR/LF characters just in case (defensive) */
		for (char *c = meta->id; *c; ++c) { if (*c=='\r' || *c=='\n') { *c='\0'; break; } }
		for (char *c = meta->name; *c; ++c) { if (*c=='\r' || *c=='\n') { *c='\0'; break; } }
	}
	/* If no fields defined, fall back to defaults (ensures functional profile) */
	if (out->field_count==0) {
		filter_profile_load_defaults(out, reg, csv_mgr);
		if (meta) { strncpy(meta->name, "(defaults)", sizeof(meta->name)-1); }
	}
	/* Apply debug preference if not set by profile (supports BOOL or legacy STRING) */
	if (!file_debug) { file_debug = prefs_flag_enabled("Filter.debug", false); }
	out->debug_enabled = file_debug;
	if (out->debug_enabled) {
		for (uint8_t i=0;i<out->field_count;i++) {
			FP_FieldProfile *fpf=&out->fields[i];
			const char *fname = field_registry_get_name(reg, fpf->field_id);
			LOG_PRINTF("FP SUMMARY: field=%s id=%u weight=%u include=%u exclude=%u allow_multiple=%s\n", fname?fname:"?", fpf->field_id, fpf->weight, fpf->include_count, fpf->exclude_count, fpf->allow_multiple?"yes":"no");
		}
		LOG_PRINTF("FP SUMMARY: total_fields=%u profile='%s' name='%s' warnings=%s\n", out->field_count, meta?meta->id:"", meta?meta->name:"", (meta && meta->had_warnings)?"yes":"no");
	}
	return true;
}
/* End of Text */
