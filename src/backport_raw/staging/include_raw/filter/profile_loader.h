/* profile_loader.h - minimal stub loader for external filter profiles */
#ifndef FILTER_PROFILE_LOADER_H
#define FILTER_PROFILE_LOADER_H

#include <platform.h>
#include <filter/filter_profile.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char id[64];
	char name[128];
	uint32_t profile_format;
	uint32_t version;
	bool had_warnings;
} ProfileMeta;

/* Attempt to load a profile file (.profile). For now parse [Profile] id/name and then call defaults. */
bool profile_load_file(const char *path, FilterProfile *out, ProfileMeta *meta, const FieldRegistry *reg, GlobalCSVManager *csv_mgr);

#ifdef __cplusplus
}
#endif

#endif /* FILTER_PROFILE_LOADER_H */
/* End of Text */
