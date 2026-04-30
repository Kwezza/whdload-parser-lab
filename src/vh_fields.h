#ifndef VH_FIELDS_H
#define VH_FIELDS_H

typedef enum VhField {
    VH_FIELD_INVALID = 0,
    VH_FIELD_MEMORY,
    VH_FIELD_LANGUAGE,
    VH_FIELD_CHIPSET,
    VH_FIELD_VIDEO,
    VH_FIELD_MEDIA
} VhField;

int vh_field_from_name(const char *name, VhField *out_field);
const char *vh_field_display_name(VhField field);
const char *vh_field_csv_filename(VhField field);

#endif
