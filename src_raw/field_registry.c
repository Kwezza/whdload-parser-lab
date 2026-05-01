/* field_registry.c - Dynamic Field Registry Implementation
 *
 * Copyright © 2025 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implementation of dynamic field registry system that assigns field IDs at
 * runtime based on pack_types.ini configuration. Enables user extensibility
 * without recompilation.
 *
 * Author: GitHub Copilot
 * Created: 2025-08-06
 * Updated: auto-managed via Git
 */

#include <platform.h>
#include <tlv_filename/field_registry.h>
#include <tlv_filename/error_handling.h>
#include <io/pack_types_loader.h>
#include <platform/platform_string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*------------------------------------------------------------------------*/
/* CSV Filename Mapping Helpers */

/* Helper: trim leading/trailing spaces and tabs in place (ASCII only) */
static void trim_ws(char *s)
{
    char *start;
    char *end;
    if (!s) {
        return;
    }
    /* Trim leading */
    start = s;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    /* Trim trailing */
    end = s + strlen(s);
    while (end > s) {
        char c = *(end - 1);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            end--;
            *end = '\0';
        } else {
            break;
        }
    }
}

static void set_csv_base_for_field(FieldDefinition *field, const char *field_name)
{
    /* Default: base matches field name */
    const char *base = field_name;

    /* Special mappings where CSV basename differs from field name */
    if (strcmp(field_name, "language") == 0) {
        base = "Language"; /* CSV is capitalized */
    } else if (strcmp(field_name, "scene_group") == 0) {
        base = "demo_groups"; /* CSV file holding scene groups */
    } else if (strcmp(field_name, "kernal_version") == 0) {
        base = "variant_tags"; /* Stored in variant_tags.csv */
    } else if (strcmp(field_name, "sps") == 0) {
        base = ""; /* No CSV for SPS numbers */
    } else if (strcmp(field_name, "display_name") == 0) {
        base = ""; /* Internal display string, not CSV backed */
    }

    /* Store base name without extension; csv_cache will append .csv */
    strncpy(field->csv_filename, base, sizeof(field->csv_filename) - 1);
    field->csv_filename[sizeof(field->csv_filename) - 1] = '\0';
}

/**
 * @brief Allocate and initialize a new field registry
 */
FieldRegistry *field_registry_alloc(void)
{
    FieldRegistry *registry = whd_malloc(sizeof(FieldRegistry));
    if (!registry) {
        return NULL;
    }

    /* Initialize structure */
    memset(registry, 0, sizeof(FieldRegistry));

    /* Allocate field array */
    registry->max_fields = FIELD_ID_DYNAMIC_COUNT; /* 252 fields max */
    registry->fields = whd_malloc(registry->max_fields * sizeof(FieldDefinition));
    if (!registry->fields) {
        whd_free(registry);
        return NULL;
    }

    /* Initialize field array */
    memset(registry->fields, 0, registry->max_fields * sizeof(FieldDefinition));

    /* Set starting field ID */
    registry->next_field_id = FIELD_ID_DYNAMIC_MIN; /* Start at 0x04 */
    registry->field_count = 0;
    registry->initialized = true;

    /* Inject implicit display_name field (internal display + filtering). Raw filename moved to sidecar. */
    {
        FieldDefinition *field = &registry->fields[registry->field_count];
        field->field_id = registry->next_field_id; /* 0x04 initially */
        strncpy(field->field_name, "display_name", sizeof(field->field_name) - 1);
        field->field_name[sizeof(field->field_name) - 1] = '\0';
        set_csv_base_for_field(field, "display_name");
        field->is_active = true;
        field->allow_multiple = false;
        field->prescan_enabled = false;
        field->prescan_order = 1000;
        field->prescan_multi_token = true;
        field->prescan_remove = true;
        field->prescan_allow_multiple = false;
        field->prescan_csv_override[0] = '\0';
        field->has_default = false;
        field->default_token_id = 0;
        registry->field_count++;
        registry->next_field_id++;
    }

    return registry;
} /* field_registry_alloc */

/*------------------------------------------------------------------------*/

/**
 * @brief Free field registry and all associated memory
 */
void field_registry_free(FieldRegistry *registry)
{
    if (!registry) {
        return;
    }

    if (registry->fields) {
        whd_free(registry->fields);
    }

    whd_free(registry);
} /* field_registry_free */

/*------------------------------------------------------------------------*/

/**
 * @brief Add field to registry with runtime ID assignment
 */
static bool field_registry_add_field_internal(FieldRegistry *registry, const char *field_name)
{
    FieldDefinition *field;

    if (!registry || !field_name || !registry->initialized) {
        return false;
    }

    /* Check if we have space for more fields */
    if (registry->field_count >= registry->max_fields) {
        return false; /* Registry full */
    }

    /* Check if field already exists */
    if (field_registry_get_id(registry, field_name) != 0) {
        return true; /* Already exists, consider it success */
    }

    /* Add new field */
    field = &registry->fields[registry->field_count];
    field->field_id = registry->next_field_id;

    /* Copy field name */
    strncpy(field->field_name, field_name, sizeof(field->field_name) - 1);
    field->field_name[sizeof(field->field_name) - 1] = '\0';

    /* Assign CSV base name mapping */
    set_csv_base_for_field(field, field_name);

    field->is_active = true;
    field->allow_multiple = false; /* default unless overridden by INI attributes */
    /* Prescan defaults */
    field->prescan_enabled = false;
    field->prescan_order = 1000; /* low priority by default */
    field->prescan_multi_token = true;
    field->prescan_remove = true;
    field->prescan_allow_multiple = false; /* default align with allow_multiple unless set */
    field->prescan_csv_override[0] = '\0';
        field->has_default = false;
        field->default_token_id = 0;

    /* Update registry state */
    registry->field_count++;
    registry->next_field_id++;

    return true;
} /* field_registry_add_field_internal */

/*------------------------------------------------------------------------*/

/**
 * @brief Add field to registry with specific field ID (for metadata map reconstruction)
 */
bool field_registry_add_field(FieldRegistry *registry, const char *field_name, uint8_t field_id)
{
    FieldDefinition *field;

    if (!registry || !field_name || !registry->initialized) {
        return false;
    }

    /* Check if we have space for more fields */
    if (registry->field_count >= registry->max_fields) {
        return false; /* Registry full */
    }

    /* Check field ID lower bound (upper bound check is redundant for uint8_t) */
    if (field_id < FIELD_ID_DYNAMIC_MIN) {
        return false; /* Invalid field ID */
    }

    /* Check if field already exists */
    if (field_registry_get_id(registry, field_name) != 0) {
        return true; /* Already exists, consider it success */
    }

    /* Add new field */
    field = &registry->fields[registry->field_count];
    field->field_id = field_id;

    /* Copy field name */
    strncpy(field->field_name, field_name, sizeof(field->field_name) - 1);
    field->field_name[sizeof(field->field_name) - 1] = '\0';

    /* Assign CSV base name mapping */
    set_csv_base_for_field(field, field_name);

    field->is_active = true;
    field->allow_multiple = false; /* default unless overridden by INI attributes */
    /* Prescan defaults */
    field->prescan_enabled = false;
    field->prescan_order = 1000;
    field->prescan_multi_token = true;
    field->prescan_remove = true;
    field->prescan_allow_multiple = false;
    field->prescan_csv_override[0] = '\0';
        field->has_default = false;
        field->default_token_id = 0;

    /* Update registry state */
    registry->field_count++;

    /* Update next_field_id if this ID is higher */
    if (field_id >= registry->next_field_id) {
        registry->next_field_id = field_id + 1;
    }

    return true;
} /* field_registry_add_field */

/*------------------------------------------------------------------------*/

/**
 * @brief Parse field list string and add fields to registry
 */
static bool parse_field_list(FieldRegistry *registry, const char *field_list)
{
    char *field_list_copy;
    char *token;
    char *saveptr = NULL;
    bool success = true;

    if (!registry || !field_list) {
        return false;
    }

    /* Make copy for strtok_r */
    field_list_copy = whd_malloc(strlen(field_list) + 1);
    if (!field_list_copy) {
        return false;
    }
    strcpy(field_list_copy, field_list);

    /* Parse comma-separated field names */
    token = whd_strtok_r(field_list_copy, ",", &saveptr);
    while (token && success) {
        /* Trim whitespace */
        while (*token == ' ' || *token == '\t') {
            token++;
        }

        /* Add field to registry */
        if (strlen(token) > 0) {
            success = field_registry_add_field_internal(registry, token);
        }

        token = whd_strtok_r(NULL, ",", &saveptr);
    }

    whd_free(field_list_copy);
    return success;
} /* parse_field_list */

/*------------------------------------------------------------------------*/

/**
 * @brief Build field registry from pack_types.ini configuration
 */
bool build_field_registry_from_ini(FieldRegistry *registry, const char *pack_types_ini_path)
{
    FILE *ini_file;
    char line[512];
    char *equals_pos;
    char *field_list;
    bool in_pack_types = false;
    bool in_field_attrs = false;

    if (!registry || !pack_types_ini_path || !registry->initialized) {
        return false;
    }

    /* Open pack_types.ini file */
    ini_file = fopen(pack_types_ini_path, "r");
    if (!ini_file) {
        return false;
    }

    /* Parse each line */
    while (fgets(line, sizeof(line), ini_file)) {
        /* Skip comments and empty lines */
        if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        /* Trim trailing newline */
        char *nl = strchr(line, '\n'); if (nl) { *nl = '\0'; }
        nl = strchr(line, '\r'); if (nl) { *nl = '\0'; }

        /* Section headers */
        if (line[0] == '[') {
            in_pack_types = (strcmp(line, "[PackTypes]") == 0);
            in_field_attrs = (strcmp(line, "[FieldAttributes]") == 0 || strcmp(line, "[field_attributes]") == 0);
            continue;
        }

        /* Handle FieldAttributes section */
        if (in_field_attrs) {
            char *dot = strchr(line, '.');
            char *eq = strchr(line, '=');
            if (!dot || !eq || dot > eq) {
                continue; /* malformed */
            }
            *dot = '\0';
            *eq = '\0';
            char *field_name = line;
            char *attr = dot + 1;
            char *val = eq + 1;

            /* Trim spaces around all components */
            trim_ws(field_name);
            trim_ws(attr);
            trim_ws(val);

            if (strcmp(attr, "allow_multiple") == 0) {
                bool allow = (whd_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || whd_strcasecmp(val, "yes") == 0);
                (void)field_registry_set_allow_multiple(registry, field_name, allow);
#if PLATFORM_HOST
                /* Silenced verbose debug of FieldAttributes in normal runs */
#endif
            } else if (strcmp(attr, "prescan.enabled") == 0) {
                bool enable = (whd_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || whd_strcasecmp(val, "yes") == 0);
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        registry->fields[i].prescan_enabled = enable;
                        break;
                    }
                }
            } else if (strcmp(attr, "prescan.order") == 0) {
                unsigned ord = (unsigned)atoi(val);
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        registry->fields[i].prescan_order = (uint16_t)ord;
                        break;
                    }
                }
            } else if (strcmp(attr, "prescan.multi_token") == 0) {
                bool mt = (whd_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || whd_strcasecmp(val, "yes") == 0);
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        registry->fields[i].prescan_multi_token = mt;
                        break;
                    }
                }
            } else if (strcmp(attr, "prescan.remove_from_filename") == 0) {
                bool rm = (whd_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || whd_strcasecmp(val, "yes") == 0);
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        registry->fields[i].prescan_remove = rm;
                        break;
                    }
                }
            } else if (strcmp(attr, "prescan.allow_multiple") == 0) {
                bool am = (whd_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || whd_strcasecmp(val, "yes") == 0);
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        registry->fields[i].prescan_allow_multiple = am;
                        break;
                    }
                }
            } else if (strcmp(attr, "prescan.source") == 0) {
                /* Accept either a base name like 'contributors' or a full path; we store base */
                const char *src = val;
                /* Trim quotes/spaces on both ends */
                while (*src == ' ' || *src == '\t' || *src == '"') src++;
                char buf[64];
                size_t l = strlen(src);
                while (l > 0 && (src[l-1] == ' ' || src[l-1] == '\t' || src[l-1] == '"')) { l--; }
                if (l >= sizeof(buf)) { l = sizeof(buf) - 1; }
                /* Strip directory and extension if present */
                const char *base = src;
                for (size_t i = 0; i < l; i++) { if (src[i] == '/' || src[i] == '\\') { base = src + i + 1; } }
                const char *dot = NULL; for (size_t i = 0; i < l; i++) { if (src[i] == '.') { dot = src + i; break; } }
                size_t blen = dot ? (size_t)(dot - base) : (size_t)(src + l - base);
                if (blen >= sizeof(buf)) { blen = sizeof(buf) - 1; }
                /* Safe copy base into local buf */
                if (blen > 0) {
                    memcpy(buf, base, blen);
                    buf[blen] = '\0';
                } else {
                    buf[0] = '\0';
                }
                for (uint32_t i = 0; i < registry->field_count; i++) {
                    if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
                        /* Safe bounded copy into prescan_csv_override */
                        size_t cpy = strlen(buf);
                        if (cpy >= sizeof(registry->fields[i].prescan_csv_override)) {
                            cpy = sizeof(registry->fields[i].prescan_csv_override) - 1;
                        }
                        memcpy(registry->fields[i].prescan_csv_override, buf, cpy);
                        registry->fields[i].prescan_csv_override[cpy] = '\0';
                        break;
                    }
                }
            }
            continue;
        }

        /* Only process field lists inside PackTypes */
        if (!in_pack_types) {
            continue;
        }

        /* Find equals sign */
        equals_pos = strchr(line, '=');
        if (!equals_pos) {
            continue;
        }

        /* Find field list (last component after pipe separators) */
        field_list = strrchr(equals_pos + 1, '|');
        if (field_list) {
            field_list++; /* Skip the pipe */
        } else {
            continue; /* No field list found */
        }

        /* Parse field list and add fields */
        if (!parse_field_list(registry, field_list)) {
            fclose(ini_file);
            return false;
        }
    }

    fclose(ini_file);
    return true;
} /* build_field_registry_from_ini */

/*------------------------------------------------------------------------*/

/**
 * @brief Get runtime field ID for a field name
 */
uint8_t field_registry_get_id(const FieldRegistry *registry, const char *field_name)
{
    uint32_t i;

    if (!registry || !field_name || !registry->initialized) {
        return 0;
    }

    /* Search for field by name */
    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active &&
            strcmp(registry->fields[i].field_name, field_name) == 0) {
            return registry->fields[i].field_id;
        }
    }

    return 0; /* Not found */
} /* field_registry_get_id */

/*------------------------------------------------------------------------*/

/**
 * @brief Get field name for a runtime field ID
 */
const char *field_registry_get_name(const FieldRegistry *registry, uint8_t field_id)
{
    uint32_t i;

    if (!registry || !registry->initialized) {
        return NULL;
    }

    /* Search for field by ID */
    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active &&
            registry->fields[i].field_id == field_id) {
            return registry->fields[i].field_name;
        }
    }

    return NULL; /* Not found */
} /* field_registry_get_name */

/*------------------------------------------------------------------------*/

/**
 * @brief Get CSV filename for a field name
 */
const char *get_csv_filename_for_field(const FieldRegistry *registry, const char *field_name)
{
    uint32_t i;

    if (!registry || !field_name || !registry->initialized) {
        return NULL;
    }

    /* Search for field by name */
    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active &&
            strcmp(registry->fields[i].field_name, field_name) == 0) {
            /* Prefer a prescan-defined CSV override if present */
            if (registry->fields[i].prescan_csv_override[0] != '\0') {
                return registry->fields[i].prescan_csv_override;
            }
            if (registry->fields[i].csv_filename[0] == '\0') {
                return NULL; /* No CSV for this field */
            }
            return registry->fields[i].csv_filename; /* base name without .csv */
        }
    }

    return NULL; /* Not found */
} /* get_csv_filename_for_field */

/*------------------------------------------------------------------------*/

/**
 * @brief Check if field registry has available field IDs
 */
bool field_registry_has_available_ids(const FieldRegistry *registry)
{
    if (!registry || !registry->initialized) {
        return false;
    }

    return (registry->field_count < registry->max_fields);
} /* field_registry_has_available_ids */

/*------------------------------------------------------------------------*/

/**
 * @brief Get count of registered fields
 */
uint32_t field_registry_get_count(const FieldRegistry *registry)
{
    if (!registry || !registry->initialized) {
        return 0;
    }

    return registry->field_count;
} /* field_registry_get_count */

/*------------------------------------------------------------------------*/

/**
 * @brief Validate field registry for potential issues
 */
bool field_registry_validate(const FieldRegistry *registry)
{
    uint32_t i, j;

    if (!registry || !registry->initialized) {
        return false;
    }

    /* Check for duplicate field names */
    for (i = 0; i < registry->field_count; i++) {
        if (!registry->fields[i].is_active) {
            continue;
        }

        for (j = i + 1; j < registry->field_count; j++) {
            if (!registry->fields[j].is_active) {
                continue;
            }

            /* Check for duplicate field names (case insensitive) */
            if (whd_strcasecmp(registry->fields[i].field_name, registry->fields[j].field_name) == 0) {
                return false; /* Duplicate found */
            }
        }
    }

    /* Check field ID range */
    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active) {
            /* Lower-bound check; upper bound is implicit for uint8_t */
            if (registry->fields[i].field_id < FIELD_ID_DYNAMIC_MIN) {
                return false; /* Field ID out of range */
            }
        }
    }

    return true;
} /* field_registry_validate */

/*------------------------------------------------------------------------*/

bool field_registry_set_allow_multiple(FieldRegistry *registry, const char *field_name, bool allow)
{
    uint32_t i;

    if (!registry || !field_name || !registry->initialized) {
        return false;
    }

    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
            registry->fields[i].allow_multiple = allow;
            return true;
        }
    }
    return false;
}

bool field_registry_get_allow_multiple(const FieldRegistry *registry, const char *field_name)
{
    uint32_t i;

    if (!registry || !field_name || !registry->initialized) {
        return false;
    }

    for (i = 0; i < registry->field_count; i++) {
        if (registry->fields[i].is_active && strcmp(registry->fields[i].field_name, field_name) == 0) {
            return registry->fields[i].allow_multiple;
        }
    }
    return false;
}

/*------------------------------------------------------------------------*/
/* Prescan configuration accessors */

bool field_registry_get_prescan_config(const FieldRegistry *registry,
                                       const char *field_name,
                                       FieldPrescanConfig *out_cfg) {
    if (!registry || !field_name || !out_cfg) {
        return false;
    }
    for (uint32_t i = 0; i < registry->field_count; i++) {
        const FieldDefinition *fd = &registry->fields[i];
        if (fd->is_active && strcmp(fd->field_name, field_name) == 0) {
            out_cfg->field_name = fd->field_name;
            out_cfg->csv_base = (fd->prescan_csv_override[0] ? fd->prescan_csv_override : fd->csv_filename);
            out_cfg->field_id = fd->field_id;
            out_cfg->order = fd->prescan_order;
            out_cfg->enabled = fd->prescan_enabled;
            out_cfg->multi_token = fd->prescan_multi_token;
            out_cfg->remove_from_filename = fd->prescan_remove;
            out_cfg->allow_multiple = fd->prescan_allow_multiple ? true : fd->allow_multiple;
            return true;
        }
    }
    return false;
}

uint32_t field_registry_list_prescan_fields(const FieldRegistry *registry,
                                            FieldPrescanConfig *out,
                                            uint32_t max_items) {
    if (!registry || !out || max_items == 0) {
        return 0;
    }
    /* Gather enabled prescan fields */
    uint32_t count = 0;
    for (uint32_t i = 0; i < registry->field_count && count < max_items; i++) {
        const FieldDefinition *fd = &registry->fields[i];
        if (fd->is_active && fd->prescan_enabled) {
            out[count].field_name = fd->field_name;
            out[count].csv_base = (fd->prescan_csv_override[0] ? fd->prescan_csv_override : fd->csv_filename);
            out[count].field_id = fd->field_id;
            out[count].order = fd->prescan_order;
            out[count].enabled = true;
            out[count].multi_token = fd->prescan_multi_token;
            out[count].remove_from_filename = fd->prescan_remove;
            out[count].allow_multiple = fd->prescan_allow_multiple ? true : fd->allow_multiple;
            count++;
        }
    }
    /* Simple insertion sort by order (small arrays) */
    for (uint32_t i = 1; i < count; i++) {
        FieldPrescanConfig key = out[i];
        int j = (int)i - 1;
        while (j >= 0 && out[j].order > key.order) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

/*------------------------------------------------------------------------*/

bool field_registry_set_default(FieldRegistry *registry, const char *field_name, uint16_t token_id) {
    if (!registry || !field_name || token_id == 0) { return false; }
    for (uint32_t i=0;i<registry->field_count;i++) {
        FieldDefinition *fd = &registry->fields[i];
        if (fd->is_active && strcmp(fd->field_name, field_name)==0) {
            fd->has_default = true;
            fd->default_token_id = token_id;
            return true;
        }
    }
    return false;
}

uint16_t field_registry_get_default_token_id(const FieldRegistry *registry, uint8_t field_id, bool *has_default) {
    if (!registry || field_id==0) { if(has_default){*has_default=false;} return 0; }
    for (uint32_t i=0;i<registry->field_count;i++) {
        const FieldDefinition *fd = &registry->fields[i];
        if (fd->is_active && fd->field_id == field_id) {
            if (has_default) { *has_default = fd->has_default; }
            return fd->has_default ? fd->default_token_id : 0;
        }
    }
    if (has_default) { *has_default = false; }
    return 0;
}

/*------------------------------------------------------------------------*/

/**
 * @brief Build field registry from pack types array
 */
ProcessingResult build_field_registry_from_pack_types(FieldRegistry *registry,
                                                      const PackType *pack_types,
                                                      size_t pack_count,
                                                      ProcessingError *error)
{
    if (!registry || !pack_types || pack_count == 0) {
        if (error) {
            processing_error_set(error, PROCESSING_ERROR_INVALID_INPUT,
                                NULL, NULL, "Invalid input parameters for field registry build");
        }
        return PROCESSING_ERROR_INVALID_INPUT;
    }

    /* Build field registry from pack types */
    for (size_t i = 0; i < pack_count; i++) {
        const PackType *pack = &pack_types[i];

        /* Add each field from the pack type */
        for (size_t j = 0; j < pack->num_fields; j++) {
            if (pack->field_list[j] && strlen(pack->field_list[j]) > 0) {
                /* Check if field already exists */
                uint8_t existing_id = field_registry_get_id(registry, pack->field_list[j]);
                if (existing_id == 0) {
                    /* Field doesn't exist, add it */
                    uint8_t next_id = registry->next_field_id;
                    if (field_registry_add_field(registry, pack->field_list[j], next_id)) {
                        registry->next_field_id = next_id + 1;
                    } else {
                        if (error) {
                            processing_error_set(error, PROCESSING_ERROR_MEMORY_ALLOCATION,
                                                pack->field_list[j], NULL,
                                                "Failed to add field to registry");
                        }
                        return PROCESSING_ERROR_MEMORY_ALLOCATION;
                    }
                }
            }
        }
    }

    if (error) {
        processing_error_clear(error);
    }
    return PROCESSING_SUCCESS;
} /* build_field_registry_from_pack_types */

/*------------------------------------------------------------------------*/

/* End of Text */
