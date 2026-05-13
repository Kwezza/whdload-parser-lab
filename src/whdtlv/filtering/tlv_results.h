/* filtering/tlv_results.h - Write filter results to disk and print summary
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * File-writing and console-summary responsibilities are isolated here so
 * they can be removed or redirected later without touching selector logic.
 *
 * Initial output format: one selected archive filename per line.
 * Extended CSV format (filename,size_kib,crc32,score,group) is deferred
 * until the plain filename output is proven correct.
 */

#ifndef FILTERING_TLV_RESULTS_H
#define FILTERING_TLV_RESULTS_H

#include "platform.h"
#include "whdtlv/whdtlv.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/tlv_select.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/* API                                                                    */

/*
 * Write one selected archive filename per line to output_path.
 * Returns WHD_FILTER_OK or WHD_FILTER_ERR_OUTPUT_WRITE.
 */
int tlv_results_write_file(const char            *output_path,
                           const WhdSelectResult *sel,
                           const WhdGroupSet     *gs,
                           const WhdVariantArray *arr);

/*
 * Collect selected archive filenames into an in-memory list.
 *
 * Uses a single contiguous allocation so the pointer table and all string
 * data share one block.  list->reserved owns the allocation; list->items
 * points into it.  Free with whdtlv_string_list_free() (Phase 4).
 *
 * Returns WHD_FILTER_OK on success (including empty result set).
 * On failure the function returns a negative error code and leaves *list
 * in a safe empty state.
 */
int tlv_results_collect_list(const WhdSelectResult *sel,
                              const WhdGroupSet     *gs,
                              const WhdVariantArray *arr,
                              WhdTlvStringList      *list);

/*
 * Print the standard console summary to stdout.
 * result may be NULL; fields that are NULL are shown as "(none)".
 */
void tlv_results_print_summary(const char            *tlv_path,
                               const char            *profile_path,
                               const char            *output_path,
                               const WhdFilterResult *result);

#ifdef __cplusplus
}
#endif

#endif /* FILTERING_TLV_RESULTS_H */
/* End of Text */
