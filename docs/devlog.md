# Dev Log

## 2026-04-30 - vh_group.c sort-path optimization for 68000 targets

### Summary

Implemented a low-risk performance refactor in `src/vh_group.c` to reduce hot-path sorting cost on constrained Amiga-class targets.

### What Changed

1. Replaced the previous quadratic order sort with iterative heapsort:
   - Old behavior: nested-loop comparison sort (`O(n^2)`).
   - New behavior: non-recursive heapsort over the existing index array (`O(n log n)`, worst-case).

2. Reused report-pass group run information for memory-estimate calculations:
   - `vh_group_print_report` now tracks `largest_duplicate_group_size` during its existing run scan.
   - `vh_print_memory_estimate` now accepts this precomputed value instead of re-sorting internally.

3. Preserved existing data structures and output format:
   - No changes to public headers or CLI behavior.
   - No changes to selection tie-break semantics.

### Why This Was Done

This project targets stock 68000-class systems with finite RAM and CPU budget. The prior repeated quadratic sorting in grouping/reporting paths was an avoidable hotspot on larger DAT inputs.

The chosen fix is intentionally conservative:
- Uses a simple in-place algorithm with no recursion.
- Keeps memory usage stable (reuses existing order array pattern).
- Reduces CPU cost significantly for larger candidate lists without architectural churn.

### Validation

Ran after patch:
- `make`
- `make test`

Observed results:
- Build succeeded.
- Unit tests passed (`vh_string_pool`, `vh_parse_group_key`).
- Milestone harness passed including baseline comparisons and Phase 3 checks.
