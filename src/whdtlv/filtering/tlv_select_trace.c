/* src/whdtlv/filtering/tlv_select_trace.c - Selection trace collector
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Implements the growable row collector used by tlv_select_run_traced().
 * Compiled only when WHDTLV_ENABLE_SELECTION_TRACE is non-zero.
 *
 * C89-compatible; vbcc-safe.
 */

#if WHDTLV_ENABLE_SELECTION_TRACE

#include "whdtlv/filtering/tlv_select_trace.h"
#include <stdlib.h>
#include <string.h>

#define TRACE_INITIAL_CAPACITY 256ul

void whdtlv_trace_init(WhdTlvSelectionTrace *trace)
{
    if (!trace) { return; }
    trace->rows     = NULL;
    trace->count    = 0ul;
    trace->capacity = 0ul;
}

void whdtlv_trace_free(WhdTlvSelectionTrace *trace)
{
    if (!trace) { return; }
    free(trace->rows);
    trace->rows     = NULL;
    trace->count    = 0ul;
    trace->capacity = 0ul;
}

int whdtlv_trace_add_row(WhdTlvSelectionTrace          *trace,
                          const WhdTlvSelectionTraceRow *row)
{
    unsigned long            new_cap;
    WhdTlvSelectionTraceRow *new_rows;

    if (!trace || !row) { return -1; }

    if (trace->count >= trace->capacity) {
        new_cap  = (trace->capacity == 0ul)
                   ? TRACE_INITIAL_CAPACITY
                   : trace->capacity * 2ul;
        new_rows = (WhdTlvSelectionTraceRow *)realloc(
                       trace->rows,
                       new_cap * sizeof(WhdTlvSelectionTraceRow));
        if (!new_rows) { return -1; }
        trace->rows     = new_rows;
        trace->capacity = new_cap;
    }

    memcpy(&trace->rows[trace->count], row, sizeof(*row));
    trace->count++;
    return 0;
}

#endif /* WHDTLV_ENABLE_SELECTION_TRACE */
/* End of Text */
