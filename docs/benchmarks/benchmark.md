# Default Benchmark - 68030 @ 40MHz

Date: 2026-05-01
Repository: GUI-WHDload-downloader
Branch: DevTLV
Working folder: variant_backport_staging

## Purpose

This document defines the current default benchmark configuration for the standalone `dat_to_tlv` tool and records what the latest profiling data is showing.

This benchmark should be treated as the primary reference point when evaluating future TLV pipeline optimizations unless a newer default baseline is explicitly chosen.

## Default Benchmark Configuration

Current default benchmark environment:

- CPU: 68030 @ 40MHz
- Runtime: WinUAE emulation
- Chip RAM: 2 MB
- Fast RAM: 256 MB Z3 Fast (emulated)
- Build mode: `PROFILE=1`
- Input DAT: `assets_raw/Games(19-05-2025).dat`
- Output TLV: `output/Games(19-05-2025).tlv`
- CSV folder: `assets_raw/defs`
- Pack types: `assets_raw/prefs/pack_types.ini`

Important note:

- Earlier notes and filenames may refer to other machines or less precise labels.
- The current default reference benchmark should be treated as the 68030 @ 40MHz WinUAE setup above.

## Latest Default Benchmark Result

Active default baseline profiled run summary:

```text
DAT input:    assets_raw/Games(19-05-2025).dat
Output TLV:   output/Games(19-05-2025).tlv
CSV folder:   assets_raw/defs
Pack types:   assets_raw/prefs/pack_types.ini
DAT entries:  3861
Processed:    3861
Successful:   3861
Errors:       0
TLV entries:  11634
TLV build time: 166720 ms
TLV save time: 1540 ms
TLV pipeline profile (save excluded):
session_init       calls=1      total=100      ms avg=100    ms max=100    ms share=  0.0%
batch_total        calls=1      total=160440   ms avg=160440 ms max=160440 ms share=100.0%
record_init        calls=3861   total=1380     ms avg=0      ms max=20     ms share=  0.8%
process_filename   calls=3861   total=157400   ms avg=40     ms max=260    ms share= 98.1%
sanitize           calls=3861   total=1140     ms avg=0      ms max=20     ms share=  0.7%
prescan            calls=3861   total=103040   ms avg=26     ms max=100    ms share= 64.2%
prescan_join       calls=48086  total=12980    ms avg=0      ms max=20     ms share=  8.0%
prescan_lookup     calls=48086  total=57380    ms avg=1      ms max=60     ms share= 35.7%
prescan_rebuild    calls=137    total=80       ms avg=0      ms max=20     ms share=  0.0%
tokenize           calls=3861   total=4180     ms avg=1      ms max=20     ms share=  2.6%
token_loop         calls=3861   total=38300    ms avg=9      ms max=220    ms share= 23.8%
token_checks       calls=7657   total=12640    ms avg=1      ms max=20     ms share=  7.8%
pack_field_match   calls=918    total=22220    ms avg=24     ms max=220    ms share= 13.8%
unknown_token      calls=21     total=0        ms avg=0      ms max=0      ms share=  0.0%
csv_lookup         calls=60758  total=52480    ms avg=0      ms max=60     ms share= 32.7%
tlv_add_entry      calls=23268  total=6140     ms avg=0      ms max=40     ms share=  3.8%
aggregate_merge    calls=1      total=6280     ms avg=6280   ms max=6280   ms share=  3.9%
```

### Attempted optimization: prescan memoization experiment (reverted)

The first prescan-local memoization attempt was measured and then reverted.

Experimental profiled run summary:

```text
DAT input:    assets_raw/Games(19-05-2025).dat
Output TLV:   output/Games(19-05-2025).tlv
CSV folder:   assets_raw/defs
Pack types:   assets_raw/prefs/pack_types.ini
DAT entries:  3861
Processed:    3861
Successful:   3861
Errors:       0
TLV entries:  11634
TLV build time: 185980 ms
TLV save time: 1560 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=100      ms avg=100    ms max=100    ms share=  0.0%
batch_total        calls=1      total=179700   ms avg=179700 ms max=179700 ms share=100.0%
record_init        calls=3861   total=1300     ms avg=0      ms max=20     ms share=  0.7%
process_filename   calls=3861   total=175960   ms avg=45     ms max=280    ms share= 97.9%
sanitize           calls=3861   total=1160     ms avg=0      ms max=20     ms share=  0.6%
prescan            calls=3861   total=120240   ms avg=31     ms max=100    ms share= 66.9%
prescan_join       calls=48086  total=12720    ms avg=0      ms max=20     ms share=  7.0%
prescan_lookup     calls=48086  total=68120    ms avg=1      ms max=60     ms share= 37.9%
prescan_rebuild    calls=137    total=140      ms avg=1      ms max=20     ms share=  0.0%
tokenize           calls=3861   total=4220     ms avg=1      ms max=20     ms share=  2.3%
token_loop         calls=3861   total=39080    ms avg=10     ms max=240    ms share= 21.7%
token_checks       calls=7657   total=13200    ms avg=1      ms max=20     ms share=  7.3%
pack_field_match   calls=918    total=22460    ms avg=24     ms max=240    ms share= 12.4%
unknown_token      calls=21     total=0        ms avg=0      ms max=0      ms share=  0.0%
csv_lookup         calls=60758  total=53260    ms avg=0      ms max=60     ms share= 29.6%
tlv_add_entry      calls=23268  total=6300     ms avg=0      ms max=20     ms share=  3.5%
aggregate_merge    calls=1      total=6280     ms avg=6280   ms max=6280   ms share=  3.4%
```

This experimental run preserved functional correctness:

- DAT entries stayed at `3861`
- Processed stayed at `3861`
- Successful stayed at `3861`
- Errors stayed at `0`
- TLV entries stayed at `11634`

However, it regressed performance badly enough that the memo layer was removed.

Key deltas versus the active 68030 baseline:

- TLV build time: `166720 ms` -> `185980 ms` (`+19260 ms`, about `+11.6%`)
- batch_total: `160440 ms` -> `179700 ms` (`+19260 ms`, about `+12.0%`)
- process_filename: `157400 ms` -> `175960 ms` (`+18560 ms`, about `+11.8%`)
- prescan: `103040 ms` -> `120240 ms` (`+17200 ms`, about `+16.7%`)
- prescan_lookup: `57380 ms` -> `68120 ms` (`+10740 ms`, about `+18.7%`)
- prescan_join: `12980 ms` -> `12720 ms` (`-260 ms`, about `-2.0%`)
- token_loop: `38300 ms` -> `39080 ms` (`+780 ms`, about `+2.0%`)
- pack_field_match: `22220 ms` -> `22460 ms` (`+240 ms`, about `+1.1%`)
- csv_lookup: `52480 ms` -> `53260 ms` (`+780 ms`, about `+1.5%`)

What changed is also important:

- `prescan_join` improved slightly, which matches the cheaper joined-token builder change
- `prescan_lookup` got materially worse, which means the local lookup memo layer did not reduce enough repeated work to offset its own overhead
- the lookup call counts did not change, so the first pass did not actually shrink the number of profiled prescan lookup attempts

Why it failed matters for future work on 68000 and 68030 class CPUs:

- the memo layer added another linear scan in the hottest loop
- each probe also added more branchy control flow and more `strcmp()` work before the real lookup path even started
- on simple in-order 68k CPUs, that kind of extra per-lookup bookkeeping is often worse than a small amount of repeated direct work
- these machines reward simpler straight-line loops, fewer passes over small arrays, fewer temporary structures, and less stack or memory traffic in the hottest path

The current evidence suggests the local memo table is adding work in a path that already had limited duplicate-token reuse under this benchmark, and that tradeoff is especially poor on 68k-era hardware.

## Additional Comparison Benchmark - 68000 @ 7MHz

Comparison benchmark environment:

- CPU: 68000 @ 7MHz
- Runtime: Amiga-compatible environment
- Chip RAM: 2 MB
- Fast RAM: none
- Input DAT: `assets_raw/Games(19-05-2025).dat`
- Output TLV: `output/Games(19-05-2025).tlv`
- CSV folder: `assets_raw/defs`
- Pack types: `assets_raw/prefs/pack_types.ini`

### Current profiled run

```text
DAT input:    assets_raw/Games(19-05-2025).dat
Output TLV:   output/Games(19-05-2025).tlv
CSV folder:   assets_raw/defs
Pack types:   assets_raw/prefs/pack_types.ini
DAT entries:  3861
Processed:    3861
Successful:   3861
Errors:       0
TLV entries:  11634
TLV build time: 931580 ms
TLV save time: 7040 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=460      ms avg=460    ms max=460    ms share=  0.0%
batch_total        calls=1      total=892600   ms avg=892600 ms max=892600 ms share=100.0%
record_init        calls=3861   total=6720     ms avg=1      ms max=40     ms share=  0.7%
process_filename   calls=3861   total=874160   ms avg=226    ms max=1220   ms share= 97.9%
sanitize           calls=3861   total=7140     ms avg=1      ms max=20     ms share=  0.7%
prescan            calls=3861   total=609900   ms avg=157    ms max=520    ms share= 68.3%
prescan_join       calls=48086  total=77820    ms avg=1      ms max=40     ms share=  8.7%
prescan_lookup     calls=48086  total=333200   ms avg=6      ms max=260    ms share= 37.3%
prescan_rebuild    calls=137    total=620      ms avg=4      ms max=20     ms share=  0.0%
tokenize           calls=3861   total=20200    ms avg=5      ms max=40     ms share=  2.2%
token_loop         calls=3861   total=176800   ms avg=45     ms max=940    ms share= 19.8%
token_checks       calls=7657   total=76020    ms avg=9      ms max=120    ms share=  8.5%
pack_field_match   calls=918    total=81900    ms avg=89     ms max=900    ms share=  9.1%
unknown_token      calls=21     total=60       ms avg=2      ms max=20     ms share=  0.0%
csv_lookup         calls=60758  total=249860   ms avg=4      ms max=260    ms share= 27.9%
tlv_add_entry      calls=23268  total=38180    ms avg=1      ms max=180    ms share=  4.2%
aggregate_merge    calls=1      total=38980    ms avg=38980  ms max=38980  ms share=  4.3%
```

### Earlier non-profiled baseline

This is the earlier non-profiled result from `output/benchmark_68000_at_7mhz.txt`:

```text
DAT input:    assets_raw/Games(19-05-2025).dat
Output TLV:   output/Games(19-05-2025).tlv
CSV folder:   assets_raw/defs
Pack types:   assets_raw/prefs/pack_types.ini
DAT entries:  3861
Processed:    3861
Successful:   3861
Errors:       0
TLV entries:  11634
TLV build time: 306240 ms
TLV save time: 7140 ms
```

### What the 68000 comparison shows

- The 68000 build remains overwhelmingly CPU-bound.
- Save time is still small compared with build time.
- The profiled run is much slower than the earlier non-profiled baseline.

The key build-time comparison is:

- non-profiled 68000 build: `306240 ms`
- profiled 68000 build: `931580 ms`

That is roughly a `3.0x` slowdown from profiling instrumentation alone.

This matters when interpreting low-end Amiga results:

- the profiled run is useful for relative hotspot ranking
- the profiled run should not be used as the best estimate of real end-user conversion time
- the non-profiled baseline remains the better reference for "how long will this actually take"

### Hotspot pattern on the 68000

Even with the added profiling overhead, the same broad pattern still appears:

- `process_filename` dominates the build
- `prescan` is the largest internal hotspot
- `prescan_lookup` is a major contributor
- `pack_field_match` is the next significant token-loop cost
- `prescan_rebuild` is negligible

That means the optimization direction remains consistent across both the 68030 default benchmark and the slower 68000 comparison machine.

## What This Benchmark Shows

### 1. Save time is not the problem

The TLV save phase is only `1540 ms`, while TLV build time is `166720 ms`.

This means optimization effort should stay focused on the in-memory conversion pipeline, not disk output.

### 2. Filename processing dominates the build

The largest top-level bucket is:

- `process_filename = 157400 ms`
- `process_filename share = 98.1%`

This means nearly the entire build cost is inside the per-filename processing path.

### 3. Prescan is the largest hotspot

The largest internal bucket is:

- `prescan = 103040 ms`
- `prescan share = 64.2%`

This makes prescan the highest-priority optimization target.

### 4. The biggest prescan cost is lookup work, not rebuild work

Within prescan:

- `prescan_lookup = 57380 ms`
- `prescan_join = 12980 ms`
- `prescan_rebuild = 80 ms`

This strongly suggests the current bottleneck is repeated lookup activity during prescan, not rebuilding the stripped filename.

### 5. The token loop is still important, but secondary

The next major bucket is:

- `token_loop = 38300 ms`
- `token_loop share = 23.8%`

Within the token loop:

- `pack_field_match = 22220 ms`
- `token_checks = 12640 ms`
- `unknown_token = 0 ms`

This means pack field CSV matching is the next place to optimize after prescan lookup behavior.

### 6. Allocation and merge costs are visible, but not first priority

These are measurable but not primary bottlenecks:

- `tlv_add_entry = 6140 ms`
- `aggregate_merge = 6280 ms`
- `record_init = 1380 ms`
- `tokenize = 4180 ms`

They are worth revisiting later, but they should not be the first optimization targets.

### 7. Failed attempts should stay documented, but not define the baseline

The current benchmark does not support keeping the first prescan memoization change on performance grounds, so it has been reverted.

The most likely explanation is simple:

- the local memo table performs its own linear scan work on every profiled prescan lookup attempt
- the benchmark does not show enough repeated joined-token reuse for that extra work to pay for itself
- on 68000 and 68030 class CPUs, the added per-lookup bookkeeping is a poor trade unless it removes a much larger amount of work

## Optimization Priority Order

Based on the current default benchmark, optimization work should proceed in this order:

1. Reduce repeated prescan lookup work with a cheaper strategy than a per-attempt local memo scan.
2. Reduce pack field CSV matching cost in the token loop.
3. Reduce general CSV lookup cost across the pipeline.
4. Revisit TLV entry creation and aggregate merge only after the higher-cost lookup paths have been improved.

The 68000 comparison benchmark supports the same ordering. It also adds one important caution:

- use profiled runs to rank hotspots
- use non-profiled runs to judge real user-facing throughput

## Current Working Interpretation

The current profile suggests that the code is spending most of its time repeatedly asking questions like:

- "Does this joined prescan token exist in this CSV?"
- "Does this remaining token match any pack field CSV?"

The profile does not suggest that filename rebuilding, tokenization, or output writing are the main problems.

It also suggests that not all "avoid repeated lookup" strategies are automatically beneficial. On 68k hardware in particular, any optimization that adds another small-array scan, more branches, or more string comparisons inside the hottest loop should be treated skeptically unless it removes a clearly larger cost.

## Practical Next Step

The next optimization pass should stay on the prescan lookup path first, but it should avoid the reverted memo-table shape.

Specifically:

- prefer changes that reduce work directly rather than adding a second lookup structure in the hottest loop
- prefer a cheaper fast path, such as carrying a resolved CSV cache pointer or reducing manager-level csv name resolution overhead before adding another per-lookup structure
- if a cache is reconsidered later, prove first that duplicate joined tokens are common enough and that the cache probe itself is cheaper on 68k hardware than the work it is meant to remove
- inspect whether pack field CSV matching can avoid repeated scans once earlier checks already prove a token class

## Benchmark Discipline

When comparing future optimization results, always compare against this default benchmark unless another benchmark is explicitly declared as the new baseline.

A future replacement baseline should only be adopted if:

- it is clearly documented here
- the hardware or emulation configuration is stated explicitly
- the full profile summary is captured alongside total build and save time

When profiling on very slow systems such as a 68000 with no Fast RAM, expect profiling overhead to materially distort wall-clock time. Preserve both kinds of numbers when possible:

- profiled build for hotspot analysis
- non-profiled build for realistic elapsed time
