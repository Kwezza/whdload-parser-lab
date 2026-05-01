#ifndef DAT_PARSER_MINIMAL_H
#define DAT_PARSER_MINIMAL_H

#include <platform.h>
#include <stddef.h>

size_t parse_dat_filenames_minimal(const char *dat_path, char ***out_names);
void free_dat_filenames_minimal(char **names, size_t count);

#endif /* DAT_PARSER_MINIMAL_H */
