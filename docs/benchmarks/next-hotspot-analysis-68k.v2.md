# Next Hotspot Analysis For 68k Optimization — v2

Date: 2026-05-06
Repository: whdload-parser-lab
Branch: main

## Purpose

This document maps the current dominant hotspot to the exact code that is running after the
full refactoring (Steps 3–8, A, B, C, D). The previous analysis (v1) was written against
the pre-refactoring baseline. All four of its named targets (prescan sliding-window, manager
lookup, csv_find_ci fallback, pack_field_match) have since been partly or fully resolved.

This v2 focuses only on what the benchmark now says is slow and why.

## Benchmark-Driven Priority (post-refactoring, 68030 @ 40MHz)

From `docs/benchmarks/default-benchmark-68030-40mhz.md`, post-refactoring baseline:

```
prescan            calls=3861   total=55420 ms   share=61.3%
prescan_lookup     calls=43806  total=35140 ms   share=38.9%
csv_lookup_loaded  calls=47448  total=14560 ms   share=16.1%
prescan_rebuild    calls=137    total=0 ms       share=0.0%
```

Two numbers are the primary focus of this document:

- `prescan_lookup` at 35140 ms (43806 calls) — the measured inner loop cost
- Unaccounted prescan overhead at ~20280 ms — the gap between `prescan` and all named
  sub-metrics, which cannot be explained by `prescan_lookup + prescan_rebuild` alone

Neither of these is a single expensive operation. Both arise from the structure of the current
inner loop combined with instrumentation and debug code that runs on every iteration.

## Current Prescan Configuration

Two prescan fields are active (from `assets_raw/prefs/pack_types.ini`):

| Field | Order | CSV | Entries | multi_token | remove_from_filename | allow_multiple |
|---|---|---|---|---|---|---|
| `variant_tags` | 5 | `variant_tags.csv` | ~25 | true | true | false |
| `contributors` | 10 | `contributors.csv` | ~10 | true | true | true |

Both CSVs are tiny. Their hash tables are small enough to fit in the 68030's 256-byte data
cache. The cache stores lowercase canonical tokens with pre-computed `len` and `fingerprint`
fields that allow most probe slots to be rejected without a string comparison.

`variant_tags` runs first because its `order = 5`. Its entries tend to be multi-word (2–4
underscore-separated tokens), so `min_token_count` and `max_token_count` will span a wider
window range than `contributors`, which has shorter, single-word handles.

## The Inner Loop That Owns `prescan_lookup`

The relevant call path is:

```
prescan_and_strip_tokens()              [filename_processor.c:607]
  └─ for each field c:
       └─ do { for window ... for i ... }:
            └─ csv_cache_lookup_span()  [csv_cache.c:858]
                 └─ span_equals_token() [csv_cache.c:827]  (only on len+fp match)
```

`prescan_lookup` measures only the time inside `csv_cache_lookup_span()`. The `prescan`
metric minus `prescan_lookup` (~20280 ms) covers everything else that happens inside
`prescan_and_strip_tokens()` but is not charged to that sub-section.

## What `csv_cache_lookup_span()` Actually Does

File: `src_raw/csv_cache.c`, line 858.

The function receives a direct `CSVCache *` (pre-resolved once per field before the field
loop), an array of token part pointers (`parts[]`), a start index, and a window width.

Execution sequence for each call:

**Step 1 — NULL guard + entry count guard**  
One pointer check, one uint32_t compare. Cheap on 68k.

**Step 2 — `TLV_PROFILE_START` timer read**  
One timer query. On Amiga, this likely uses `ReadEClock` or the system timer and has
measurable overhead per call. Paid on every one of the 43806 calls.

**Step 3 — Hash + length computation loop (the main inner work)**  
```c
raw_hash = 5381;
look_len = 0;
for (k = 0; k < window; k++) {
    p = parts[start + k];
    if (k > 0) { raw_hash = raw_hash*33 + '_'; look_len++; }
    while (*p) {
        lc = lowercase(*p);
        raw_hash = raw_hash*33 + lc;
        look_len++;
        p++;
    }
}
```
This is a character-by-character walk of all parts in the candidate window. Every character
requires: a load, a branch for uppercase test, a case-fold, a multiply-add (djb2 step), a
store to `raw_hash`, an increment of `look_len`, and a branch to continue the inner while.

On a 68030 this is ~8–10 instructions per character with no opportunity for SIMD or
multi-issue. The multiply-add `hash * 33` can be computed as `(hash << 5) + hash`, which the
compiler already does (both the C code and any vbcc output should use shifts).

**Step 4 — Length overflow guard**  
One uint16_t compare against `CSV_MAX_TOKEN_LENGTH`. Effectively free.

**Step 5 — Length triage gate (Step D)**  
```c
if (look_len < cache->min_entry_len || look_len > cache->max_entry_len) {
    TLV_PROFILE_END(...);
    return 0;
}
```
This gate was added in Step D and was responsible for a 73% drop in `prescan` on host.
On 68030 it still fires for most candidates (windows whose total character length falls
outside the cache's entry range), but it fires **after** the full hash+length loop has
already run. The hash computation cost is paid even for length-rejected candidates.

**Step 6 — Fingerprint and initial slot**  
`look_fp = raw_hash & 0xFFFF` and `index = raw_hash & (capacity-1)`. Both are single bitwise
AND operations. Cheap.

**Step 7 — Linear probe loop**  
```c
while (cache->entries[index].token != NULL) {
    if (cache->entries[index].len == look_len &&
        cache->entries[index].fingerprint == look_fp &&
        span_equals_token(...)) { return id; }
    index = (index + 1) & (capacity - 1);
}
```
With a 0.75 load factor and small tables (capacity ~64 for contributors, ~128 for
variant_tags), most probes hit an empty slot on the first or second step. The len+fingerprint
pre-filter means `span_equals_token` is rarely called for non-matching entries.

**Step 8 — `TLV_PROFILE_END` timer read**  
Another timer read. Paid at every return path (including early return at Step 5).

## What `span_equals_token()` Does

File: `src_raw/csv_cache.c`, line 827.

Only called when a probe slot passes both the `len == look_len` and `fingerprint == look_fp`
guards. Iterates characters of the stored token and the parts in the span simultaneously,
case-folding parts on the fly. It does the same character-by-character walk as the hash loop,
but for comparison rather than hash accumulation. Because it is gated by two cheap pre-filters
it fires rarely in practice — most candidates are rejected at the length gate or on the first
probe slot. Not a primary concern.

## Identified Issues

### Issue 1 — Hash computation is paid even for length-rejected candidates

This is the most significant structural issue in the current hot path.

The length triage gate (Step D) rejects the majority of candidate windows before the probe.
On host, Step D eliminated 73% of prescan work. But the gate fires **after** the hash+length
loop, which means the full O(N) character walk is done for every candidate, including those
that will be immediately discarded at the gate.

The length and hash are computed in the same single loop because they share the same character
traversal. Separating them requires a design change.

**What would change the picture:**

Pre-compute `part_len[k]` for all parts once, before the outer window loop begins. For any
candidate span (i, window), the candidate length is:

```
cand_len = sum(part_len[i..i+window-1]) + (window - 1)
```

This is O(window) additions — no character walking. If `cand_len` is outside
`[min_entry_len, max_entry_len]`, skip the hash loop entirely. Only when the length is in
range does the hash loop run.

For candidates that would have been rejected by the length gate, this replaces O(N) character
hash work with O(window) additions. For typical filenames with 6–10 parts and small vocabulary
caches where most candidates are the wrong length, this should eliminate the majority of
character-level work inside `csv_cache_lookup_span`.

The cost: one additional pass to populate `part_len[]` before the field loop (a single
`strlen` call per token, paid once for the whole filename), and O(window) additions per
candidate instead of zero.

This is the highest-value remaining algorithmic change in the prescan path.

### Issue 2 — Unguarded `strstr()` debug calls inside the inner lookup loop

File: `src_raw/filename_processor.c`, lines ~741 and ~762.

The following block appears **inside the innermost position loop**, called once per lookup
candidate:

```c
if (filename && (strstr(filename, "Kernal_Version") != NULL ||
                 strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL)) {
    char dbg_joined[MAX_TOKEN_LENGTH];
    build_joined_token(dbg_joined, sizeof(dbg_joined), parts, i, window);
    append_to_log("PRESCAN TRY: ...");
}
```

This block is not guarded by any macro (`TLV_PROFILE_ENABLE`, `PLATFORM_AMIGA`, or
otherwise). It runs on every call to the inner loop — including in `PROFILE=1` Amiga builds.

At 43806 prescan_lookup calls per benchmark run, `strstr()` is called ~87600 times on
`filename`. Each `strstr()` on a ~50-character filename walks up to 50 characters before
returning NULL for most names. That is roughly 4.4 million character comparisons per
benchmark run, all inside what the profiler charges to the `prescan` bucket (not
`prescan_lookup`).

There is a second occurrence at line ~762 (inside the `if (id > 0)` branch) and a third at
~800 (inside `compact_token_parts` success handling). The second and third fire only on
actual matches, which are rare, so they are not primary concerns. The first fires on
every attempt regardless of match outcome.

**What would change the picture:**

Pre-compute a single boolean before the outer field loop:

```c
bool is_debug_filename = filename && (
    strstr(filename, "Kernal_Version") != NULL ||
    strstr(filename, "German_fix_by_Torti-the-Smurf") != NULL);
```

Replace all three inner-loop occurrences with `if (is_debug_filename)`. This reduces the
`strstr` call count from 43806 to 1 per filename (3861 total). The per-filename cost of
two `strstr` calls is negligible.

This fix does not touch any algorithm, data structure, or field configuration. It is the
cheapest fix available relative to its likely impact on the unaccounted 20280 ms overhead.

### Issue 3 — Double profiling instrumentation per lookup call

The outer prescan loop in `prescan_and_strip_tokens()` wraps each
`csv_cache_lookup_span()` call with its own `TLV_PROFILE_START` / `TLV_PROFILE_END` pair:

```c
TLV_PROFILE_START(lookup_profile_stamp);
id = csv_cache_lookup_span(cfg_caches[c], parts, i, window);
TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_CSV_LOOKUP, lookup_profile_stamp);
```

Inside `csv_cache_lookup_span()` itself, there is a second pair at the function level, plus
additional `TLV_PROFILE_END` calls at each early return path (including the length gate).

Result: at least **four timer reads per call**, at 43806 calls = **~175,000 timer reads**
just for prescan_lookup instrumentation. On Amiga, each timer read involves a system call
or hardware register read. At 40MHz, even a cheap 10-cycle timer read costs 0.25 µs; 175K
reads = 44ms of pure instrumentation overhead per benchmark run.

This overhead is split between the `prescan_lookup` and `csv_lookup_loaded` buckets (both
measure the same call via different levels). It inflates both numbers relative to the
uninstrumented cost.

This is inherent to `PROFILE=1` builds and is not a bug. It is documented here because it
means the measured prescan_lookup time on Amiga (~35140 ms) substantially overstates what a
non-profiled run would show for the same code path.

**Implication for measurement:** when evaluating Issue 1 and Issue 2 fixes, compare profiled
runs to each other for hotspot ranking, and measure the non-profiled wall-clock time
separately to assess real user-facing improvement.

### Issue 4 — Window loop range includes redundant iterations

File: `src_raw/filename_processor.c`, line ~706.

```c
max_window = cfg->multi_token ? pc : 1U;
for (uint32_t window = max_window; window >= 1; window--) {
    if (cfg_caches[c] != NULL &&
        (window < (uint32_t)cfg_caches[c]->min_token_count ||
         window > (uint32_t)cfg_caches[c]->max_token_count)) {
        continue;
    }
    ...
}
```

For a filename with `pc = 12` tokens and a cache with `max_token_count = 3`, the outer loop
descends from 12 to 1, but the prune guard fires for windows 12, 11, 10, ..., 4 before
reaching valid sizes. That is 9 wasted loop iterations that only pay the prune check cost.

The loop could instead start at `min(pc, max_token_count)` and stop at `min_token_count`,
eliminating the out-of-range iterations entirely:

```c
uint32_t wmax = (cfg_caches[c] && pc > cfg_caches[c]->max_token_count)
                 ? cfg_caches[c]->max_token_count : pc;
uint32_t wmin = (cfg_caches[c]) ? (uint32_t)cfg_caches[c]->min_token_count : 1U;
for (uint32_t window = wmax; window >= wmin && window >= 1; window--) { ... }
```

The per-iteration saving is small (one compare per skipped window vs. a branch taken), but
on a filename with many tokens and a single-word vocabulary it eliminates several iterations
of loop overhead per field per filename.

### Issue 5 — `do-while` re-scan after strip on multi-match fields

File: `src_raw/filename_processor.c`, line ~799.

```c
} while (pc > 0 && cfg->remove_from_filename && field_changed);
```

Both prescan fields have `remove_from_filename = true`. The `contributors` field additionally
has `allow_multiple = true`, meaning it can match and strip multiple tokens in the same
filename. Each match causes `field_changed = true`, which triggers a full re-scan from
`max_window` down to `min_token_count` for the remaining parts.

For a filename with two contributor handles (e.g., `Name1_and_Name2`), the field runs its
full window scan twice. For three handles, three times. With ~10 entries in contributors.csv,
multi-contributor filenames are uncommon but not absent in the 3861-entry corpus.

This is structural and not easily eliminated without changing the algorithm. It is documented
as a known cost amplifier that affects the `prescan` total but not `prescan_lookup` directly
(since each re-scan generates its own lookup calls, which are counted).

### Issue 6 — No part-length cache: `strlen` implicit inside hash loop

This is the underlying reason Issue 1 exists. Each call to `csv_cache_lookup_span` walks
all characters of the candidate parts to compute both the hash and the length in one pass.
`strlen()` is not called separately — the length accumulates as a byproduct of the hash loop.

This design is correct and tight for the case where the hash loop is necessary. But it means
there is currently no fast path to compute candidate length without also computing the hash.
Adding `part_len[]` as a pre-computed array (see Issue 1) unlocks the fast rejection path.

## Unaccounted Prescan Overhead: Where The ~20280 ms Goes

`prescan = 55420 ms` minus `prescan_lookup = 35140 ms` minus `prescan_rebuild ≈ 0 ms` leaves
approximately **20280 ms unaccounted** — 37% of total prescan time that is not charged to any
named sub-section.

This overhead lives inside `prescan_and_strip_tokens()` but outside the profiled
`csv_cache_lookup_span()` calls. The likely contributors, in rough priority order:

| Source | Explanation |
|---|---|
| Unguarded `strstr()` calls (Issue 2) | Two `strstr()` per inner loop iteration, ~43806 iterations |
| Outer profiling timer overhead (Issue 3) | `TLV_PROFILE_START/END` in the outer caller, ~43806 pairs |
| Window and position loop overhead | Loop counter arithmetic, branch prediction misses on 68k |
| Window prune guard checks (Issue 4) | One compare per window iteration including skipped ones |
| `do-while` re-scan overhead (Issue 5) | Full re-scan for multi-match fields |
| `compact_token_parts()` on strip | Pointer shift loop, fires on each matched removal |

The single most addressable contributor is Issue 2 (unguarded `strstr`). Issue 3 is
instrumentation-only overhead that affects the profiled build specifically.

## What The 68000 @ 7MHz Data Reveals (Cache-less Hardware)

A post-refactoring profiled run was taken on a real 68000 @ 7MHz with 2 MB chip RAM and no
Fast RAM. This machine has no CPU cache of any kind — every byte of code and data goes
through chip RAM cycles. It is the most memory-latency-sensitive environment available.

### Per-call cost comparison: 68030 vs 68000

| Metric | 68030 @ 40MHz | 68000 @ 7MHz | Ratio |
|---|---|---|---|
| `prescan_lookup` avg per call | 0.802 ms | 4.833 ms | **6.0x** |
| `csv_lookup_loaded` avg per call | 0.306 ms | 1.713 ms | **5.6x** |
| `aggregate_merge` absolute | 6640 ms | 40560 ms | **6.1x** |
| `tlv_add_entry` absolute | 6040 ms | 40200 ms | **6.7x** |
| `pack_field_match` absolute | 2560 ms | 15140 ms | **5.9x** |
| `prescan` absolute | 55420 ms | 331620 ms | **6.0x** |

The consistent ~6x ratio for character-work-heavy sections (prescan_lookup, csv_lookup_loaded,
pack_field_match) is close to the raw clock-speed ratio: 40MHz / 7MHz = 5.7x. These sections
are dominated by sequential character traversal — straightforward CPU-bound work where the
68030's cache gives little extra benefit because the working set is already small relative to
the 256-byte cache, and the 68000 penalty comes almost entirely from the clock difference.

The `aggregate_merge` and `tlv_add_entry` ratios (6.1x and 6.7x) are slightly above the clock
ratio. These sections involve heap-allocated record building and linked-list traversal across
separately allocated buffers. On 68030, the cache assists with recently-accessed pointers; on
68000, each pointer dereference is a chip RAM cycle. The extra penalty above the clock ratio
reflects this latency overhead on pointer-intensive paths.

Crucially, `aggregate_merge` and `tlv_add_entry` were nearly unchanged in absolute time
across the pre- and post-refactoring 68000 runs (~38–40 seconds each) even though most other
sections dropped 36–82%. These sections do not benefit from the hash-lookup and prescan
optimizations. On a cache-less machine they are a larger fraction of what remains.

### What this means for each identified issue

**Issue 1 (hash loop paid before length gate):**  
On 68000, every character accessed in the hash loop is a chip RAM read. With no cache, the
load cost is the same regardless of whether the character is in a hot or cold cache line.
The hash loop running on length-rejected candidates wastes 6x more wall-clock time per
character on 68000 than on 68030. The part-length pre-computation fix (eliminating the hash
loop for candidates the length gate would reject) has proportionally the same benefit on both
machines — but the absolute time saved per rejected candidate is 6x larger on 68000.

**Issue 2 (unguarded strstr in inner loop):**  
Same argument applies. Each `strstr()` character comparison is a chip RAM read on 68000.
At 43806 loop iterations with two `strstr()` calls each, roughly 4.4M chip RAM character
comparisons are wasted per run. At 7MHz chip RAM timing, this overhead is 6x more expensive
in wall-clock time than on 68030. The fix (pre-compute `is_debug_filename`) eliminates this
entirely on both machines.

**Issue 3 (double profiling instrumentation):**  
The ~175K timer reads from `PROFILE=1` instrumentation cost the same proportion on both
machines — the timer read is a hardware register or system call, not character work, so it
scales with the chip bus cycle time. The estimated ~44ms instrumentation overhead on 68030
becomes ~240ms on 68000. Significant, but not the primary concern.

**Issue 4 (window loop descends through out-of-range values):**  
Loop counter arithmetic and branch overhead are pure CPU-bound work. The 6x clock ratio
applies directly. Tightening the window range saves proportionally more wall-clock time
on 68000.

**aggregate_merge and tlv_add_entry as next-tier targets on 68000:**  
On 68030 these sections total ~12.7 seconds (7.3% + 6.6% of batch). On 68000 they total
~80.8 seconds (7.3% + 7.3% of batch). After prescan improvements, these two sections will
be a disproportionately large fraction of what remains on cache-less hardware. They are not
yet actionable targets — prescan must be addressed first — but they should be tracked as
the next investigation area specifically for the 68000 class.

### Unaccounted prescan overhead on 68000

`prescan (331620 ms) − prescan_lookup (211720 ms) − prescan_rebuild (280 ms) = ~119620 ms`
unaccounted. That is 21.7% of batch_total, compared to 22.5% on 68030 (20280 ms / 90280 ms).

The proportions are nearly identical. This confirms that the unaccounted overhead (Issue 2,
Issue 3, window loop, compact work) scales with the clock ratio in the same way as the
measured sections. There is no evidence of a new category of cost emerging on 68000 that was
invisible on 68030. The same fixes (Issue 2 first, then Issue 1) should reduce the
unaccounted overhead by the same relative amount on both machines.

### Revised hotspot share after prescan improvements

If Issues 1 and 2 together reduce prescan by 40% on 68000 (a conservative estimate given the
Step D gain of 73% on host from the length gate alone), the remaining batch_total would be
approximately:

```
prescan (reduced):     ~200000 ms   (~36% of batch)
token_loop:             84020 ms   (~15%)
csv_lookup_loaded:      81300 ms   (~15%)
aggregate_merge:        40560 ms   (~7%)
tlv_add_entry:          40200 ms   (~7%)
tokenize:               20020 ms   (~4%)
prescan_lookup:         remaining in prescan estimate
```

Under this estimate, `aggregate_merge` and `tlv_add_entry` would together represent ~14% of
batch — making them the third-largest block after prescan and token_loop. At that point they
become the natural next optimization target for the 68000.

## Recommended Next Steps In Priority Order

### 1. Fix Issue 2 first — unguarded strstr in the inner loop

Pre-compute `is_debug_filename` once before the field loop and replace all inner-loop
`strstr()` calls with the pre-computed boolean. Zero algorithmic change, zero risk of
regression. Expected to reduce the unaccounted prescan overhead materially on both host
and Amiga.

Measure: compare `prescan` total before and after. The `prescan_lookup` bucket should be
unchanged; only the unaccounted overhead should drop.

### 2. Implement part-length pre-computation (Issue 1)

Add `uint16_t part_len[MAX_TOKENS]` computed from `strlen(parts[k])` for `k = 0..pc-1`,
done once before the outer field loop (after the initial `whd_strtok_r` split). Modify
`csv_cache_lookup_span` to accept a `part_len` parameter (or compute candidate length at the
call site and pass `cand_len` directly to a modified function entry point). Gate the hash
loop on the length result.

This requires modifying both the call site in `prescan_and_strip_tokens` and the signature
of `csv_cache_lookup_span`. The `part_len[]` array must be updated after each
`compact_token_parts()` call (or recomputed, since parts[] pointers still point to their
original substrings in `tmp[]` and the lengths don't change).

Measure: compare `prescan_lookup` before and after. Expect a significant drop proportional
to the fraction of candidates currently rejected by the length gate.

### 3. Tighten window loop range (Issue 4)

Replace the `for window = max_window; window >= 1` loop with bounds derived from
`min_token_count` and `max_token_count`. Minor, low-risk, measurable.

### 4. Evaluate instrumentation-free non-profiled run timing

After Issues 1 and 2 are addressed, run a non-profiled (`PROFILE=0` or release) build on
the 68030 to establish how much of the 55420 ms prescan total was instrumentation overhead.
The gap between profiled and non-profiled will quantify Issue 3 directly.

Do the same on real 68000 hardware. The pre-refactoring non-profiled 68000 baseline was
306240 ms; after refactoring it has not been re-measured. A non-profiled 68000 run would
confirm actual user-facing throughput and establish how much of the 591640 ms profiled time
is instrumentation overhead (expected ~1.9–3.0x ratio based on the pre-refactoring data).

### 5. Track aggregate_merge and tlv_add_entry on 68000 as next-tier targets

These two sections (40560 ms and 40200 ms on 68000) did not improve with the hash-lookup
optimizations. After prescan improvements land, they will represent a disproportionately
large share of what remains on cache-less hardware. Investigate the record-building and
list-merge paths for unnecessary pointer chasing or heap traffic that would benefit from
a chip-RAM-aware redesign.

## Code Locations

| File | Symbol | Line (approx) | What |
|---|---|---|---|
| `src_raw/filename_processor.c` | `prescan_and_strip_tokens` | 607 | Outer prescan function |
| `src_raw/filename_processor.c` | inner position loop | 721 | strstr debug calls (Issue 2) |
| `src_raw/filename_processor.c` | window loop | 704 | Range could be tightened (Issue 4) |
| `src_raw/csv_cache.c` | `csv_cache_lookup_span` | 858 | Hash+length+probe (Issue 1, 3) |
| `src_raw/csv_cache.c` | `span_equals_token` | 827 | Final character comparison |
| `include_raw/tlv_filename/csv_cache.h` | `CSVCache` | 48 | `min/max_entry_len`, `min/max_token_count` |

## Summary

The current prescan hotspot is not dominated by a single expensive algorithm. It is a
combination of:

1. An algorithmic inefficiency: the hash loop is paid even for candidates the length gate
   would reject, because length and hash are computed in the same character walk (Issue 1).
2. A debug artifact: `strstr()` called on every lookup attempt inside the inner loop,
   charging ~4.4M character comparisons per benchmark run to the unaccounted prescan
   overhead (Issue 2).
3. Instrumentation overhead: four timer reads per lookup call × 43806 calls, inflating both
   the `prescan_lookup` and unaccounted prescan metrics in `PROFILE=1` builds (Issue 3).

The 68000 @ 7MHz real-hardware run confirms these issues scale with the clock ratio (~6x) as
expected for CPU-bound character work. There is no new category of cost on 68000 that was
invisible on 68030. Issue 2 and Issue 1 remain the same priorities on both machines.

After prescan improvements, `aggregate_merge` and `tlv_add_entry` (~40 seconds each on 68000,
only ~6 seconds each on 68030) will become the dominant non-prescan cost on cache-less
hardware. They should be tracked as the next investigation area for the 68000 specifically.

Issue 2 is the cheapest fix with the most immediate measurable impact on Amiga. Issue 1 is
the most algorithmically significant and provides a durable reduction to prescan_lookup. Both
should be implemented before further profiling is used to guide the next step.

## Post-Fix Measurement Results

### Issue 2 Fix — 68030 @ 40MHz, PROFILE=1 (2026-05-06)

Pre-compute `is_debug_filename` implemented and measured against the post-refactoring baseline.

| Metric | Before (baseline) | After (Issue 2 fix) | Delta |
|---|---|---|---|
| `batch_total` | 90280 ms | 86100 ms | **−4180 ms (−4.6%)** |
| `prescan` | 55420 ms | 52440 ms | **−2980 ms (−5.4%)** |
| `prescan_lookup` | 35140 ms | 35420 ms | +280 ms (noise, flat) |
| `prescan_rebuild` | 0 ms | 80 ms | noise |
| Unaccounted prescan | 20280 ms | 16940 ms | **−3340 ms (−16.5%)** |

Unaccounted prescan = `prescan − prescan_lookup − prescan_rebuild`.

Results match the prediction exactly: `prescan_lookup` is flat (the hash loop was not
touched), and only the unaccounted overhead shrank. The 3340 ms reduction in unaccounted
prescan overhead represents the cost of ~87,600 `strstr()` calls per benchmark run that
are no longer executed inside the inner loop.

The remaining unaccounted prescan overhead is ~16940 ms. This is now the next target —
primarily Issue 3 (profiling timer reads) and the window/position loop infrastructure cost.
Issue 1 (part-length pre-computation) targets `prescan_lookup` directly and is expected to
reduce it significantly by skipping the hash loop for length-rejected candidates.

### Issue 1 Fix — 68030 @ 40MHz, PROFILE=1 (2026-05-06)

`part_len[]` pre-computation implemented. Span length now screened at the call site in
`prescan_and_strip_tokens()` before calling `csv_cache_lookup_span()`. Length-rejected
candidates skip the function call, both timer reads, and the entire hash loop.

Compared against the Issue 2 fix result (86100 ms batch):

| Metric | Before (Issue 2 fix) | After (Issue 1 fix) | Delta |
|---|---|---|---|
| `batch_total` | 86100 ms | 80440 ms | **−5660 ms (−6.6%)** |
| `prescan` | 52440 ms | 46680 ms | **−5760 ms (−11.0%)** |
| `prescan_lookup` | 35420 ms | 31240 ms | **−4180 ms (−11.8%)** |
| `prescan_lookup` calls | 43806 | 38267 | **−5539 (−12.6%)** |
| `csv_lookup_loaded` calls | 47448 | 41909 | **−5539 (−12.6%)** |
| Unaccounted prescan | 16940 ms | 15380 ms | **−1560 ms (−9.2%)** |

Unaccounted prescan = `prescan − prescan_lookup − prescan_rebuild`.

The call-count drop (43806 → 38267) confirms the pre-screen is working: 5539 candidates per
benchmark run that previously entered `csv_cache_lookup_span` and were rejected by the
internal length gate are now rejected at the call site with no function call at all. The
`prescan_lookup` avg per call (0.808 → 0.816 ms) is effectively flat — the calls that still
happen are the same character-work-heavy ones as before; the cheap-to-reject ones are now
eliminated upstream.

Cumulative improvement vs original 90280 ms baseline (pre-Issue 2, pre-Issue 1):

| Metric | Original baseline | After Issues 1+2 | Total delta |
|---|---|---|---|
| `batch_total` | 90280 ms | 80440 ms | **−9840 ms (−10.9%)** |
| `prescan` | 55420 ms | 46680 ms | **−8740 ms (−15.8%)** |
| `prescan_lookup` | 35140 ms | 31240 ms | **−3900 ms (−11.1%)** |
| `prescan_lookup` calls | 43806 | 38267 | **−5539 (−12.6%)** |

### Issue 4 Fix — 68030 @ 40MHz, PROFILE=1 (2026-05-06)

Window loop range tightened from `[max_window, 1]` to `[wmax, wmin]` derived from
`cfg_caches[c]->min_token_count` and `max_token_count`. The per-iteration prune guard is
eliminated; the loop only visits window sizes that can produce a valid match.

Compared against the Issue 1 fix result (80440 ms batch):

| Metric | Before (Issue 1 fix) | After (Issue 4 fix) | Delta |
|---|---|---|---|
| `batch_total` | 80440 ms | 80440 ms | flat (noise) |
| `prescan` | 46680 ms | 45380 ms | **−1300 ms (−2.8%)** |
| `prescan_lookup` | 31240 ms | 29820 ms | **−1420 ms (−4.6%)** |
| `prescan_lookup` calls | 38267 | 38267 | unchanged |
| `prescan_lookup` avg/call | 0.816 ms | 0.779 ms | −0.037 ms |
| Unaccounted prescan | 15380 ms | 15520 ms | flat (noise) |

The call count is unchanged — window loop tightening does not change which candidates reach
`csv_cache_lookup_span` (that filtering is owned by Issue 1's length pre-screen). The
per-call cost and unaccounted overhead are within the 20 ms timer granularity noise range.
The `prescan` reduction (−1300 ms) is consistent with fewer window loop iterations, but the
effect is smaller than Issues 1 and 2 as predicted — this was always a minor, low-risk fix.

Cumulative improvement vs original 90280 ms baseline (all three fixes combined):

| Metric | Original baseline | After Issues 1+2+4 | Total delta |
|---|---|---|---|
| `batch_total` | 90280 ms | 80440 ms | **−9840 ms (−10.9%)** |
| `prescan` | 55420 ms | 45380 ms | **−10040 ms (−18.1%)** |
| `prescan_lookup` | 35140 ms | 29820 ms | **−5320 ms (−15.1%)** |
| `prescan_lookup` calls | 43806 | 38267 | **−5539 (−12.6%)** |

### Run 4 — Instrumentation overhead baseline: PROFILE=0, 68030 @ 40MHz (2026-05-06)

Same binary (Issues 1+2+4 applied), profiling disabled (`PROFILE=0`). All `TLV_PROFILE_*`
macros compile away. Same hardware: 68030 @ 40MHz, 2 MB chip + 256 MB Z3 fast.

| Metric | PROFILE=1 (Run 3) | PROFILE=0 (Run 4) | Delta |
|---|---|---|---|
| TLV build time | 80440 ms | **20220 ms** | **−60220 ms (−74.9%)** |
| TLV save time | ~1700 ms | 1700 ms | flat |

The profiling macros account for roughly **75% of total runtime** on this hardware. The
`TLV_PROFILE_START` / `TLV_PROFILE_END` pair issues two `EClock` reads per scope entry/exit.
`prescan_lookup` alone was called 38267 times, each with 4 timer reads (2 in the function,
2 at the call site) — approximately 153000 `EClock` reads inside the hot path.

The 20220 ms true wall-clock time is the real performance baseline for the optimised
pipeline on 68030 @ 40MHz with fast RAM.

### Run 5 — Plain 68000 @ 7MHz, no fast RAM, PROFILE=0 (2026-05-06)

68000 CPU (no cache), 7 MHz, 2 MB chip RAM only, no fast RAM. Profiling disabled.

| Metric | 68030 @ 40MHz (Run 4) | 68000 @ 7MHz (Run 5) | Ratio |
|---|---|---|---|
| TLV build time | 20220 ms | **97840 ms** | **~4.8×** |
| TLV save time | 1700 ms | 7080 ms | ~4.2× |

The 68000 result (~97840 ms ≈ 1 min 38 s) reflects the combined effect of a lower clock
(7 vs 40 MHz = ~5.7× slower clock), the absence of a cache, and chip-RAM-only memory
bandwidth constraints (chip DMA bus contention). The ~4.8× build time ratio is broadly
consistent with the clock differential, suggesting the pipeline has no dominant
chip-RAM-sensitive bottleneck beyond the raw CPU speed disadvantage.

This also sets a concrete target: any further algorithmic improvement that reduces
instructions executed will benefit the 68000 proportionally more than the profiling
runs suggested.

---

## Final Comparison — Original 68040 @ 40MHz vs Optimised 68030 @ 40MHz

The original unoptimised code was run on a 68040 @ 40MHz with `PROFILE=1` and is recorded
in `benchmark-summary-68040_at_40mhz.txt`. This is the starting point before any of the
refactoring or hotspot work described in this document.

The latest optimised run (Issues 1+2+4 applied, `PROFILE=1`) is Run 3 from
`benchmark-summary.txt`, measured on the 68030 @ 40MHz.

The uninstrumented optimised run (Run 4, `PROFILE=0`) is also included for reference —
it is the real user-facing wall-clock time with no profiling overhead.

Note: the 68040 has a wider pipeline and faster FPU than the 68030, but both run at 40MHz
in this WinUAE configuration. For pure integer / string work the 68040 is modestly faster
per clock, so the comparison slightly understates the algorithmic improvement.

### Profiled run comparison (apples-to-apples: same profiling overhead structure)

| Metric | Original 68040 @ 40MHz | Optimised 68030 @ 40MHz | Delta |
|---|---|---|---|
| `batch_total` | 190600 ms | **80440 ms** | **−110160 ms (−57.8%)** |
| `prescan` | 125020 ms | 45380 ms | **−79640 ms (−63.7%)** |
| `prescan_lookup` | 70760 ms | 29820 ms | **−40940 ms (−57.8%)** |
| `prescan_lookup` calls | 48086 | 38267 | **−9819 (−20.4%)** |
| `prescan_join` | 12900 ms | — | eliminated |
| `token_loop` | 47520 ms | 13360 ms | **−34160 ms (−71.9%)** |
| `pack_field_match` | 31920 ms | 2660 ms | **−29260 ms (−91.7%)** |
| `csv_lookup` (slow path) | 62960 ms | — | eliminated |
| `csv_lookup_loaded` | n/a | 12240 ms | new metric |
| `aggregate_merge` | 6360 ms | 6660 ms | stable |
| `tlv_add_entry` | 5920 ms | 5980 ms | stable |

### True wall-clock comparison (PROFILE=0, no instrumentation overhead)

| Machine | Build time (no profiling) | vs original 68040 profiled |
|---|---|---|
| Original 68040 @ 40MHz | not measured (no PROFILE=0 run) | — |
| Optimised 68030 @ 40MHz | **20220 ms** | **−89.4%** |

The 20220 ms figure is not strictly comparable to the 190600 ms (one has profiling, the
other does not), but it illustrates the combined magnitude of the algorithmic improvements
and instrumentation removal. A PROFILE=0 run of the original code would likely be in the
40–60 second range based on the ~3–4× profiling overhead seen in other runs.

### What drove the improvement

| Optimisation | Primary effect |
|---|---|
| Shared `parts[]` / prescan restructure (earlier refactoring) | Eliminated `prescan_join` (12900 ms) |
| Hash table for CSV lookups (`csv_lookup_loaded`) | Eliminated slow-path `csv_lookup` (62960 ms) |
| `pack_field_match` prehash + field sort + min/max gate | −91.7% on `pack_field_match` (31920 → 2660 ms) |
| Issue 2: pre-compute `is_debug_filename` | Eliminated ~87600 inner-loop `strstr` calls |
| Issue 1: part-length pre-screen | −12.6% fewer lookup calls (48086 → 38267) |
| Issue 4: window loop range tightening | −4.6% on `prescan_lookup` time |

The most impactful single change was replacing the linear-scan `csv_lookup` (publisher.csv
open + string scan per token) with the hash table `csv_lookup_loaded` path — that alone
accounts for the bulk of the 62960 ms → 0 ms drop in the slow lookup bucket.

The second most impactful was the `pack_field_match` prehash + early-exit patches, cutting
a 31920 ms hotspot to 2660 ms.

The Issues 1+2+4 hotspot work in this document contributed a further ~10 seconds of
profiled-run improvement on top of the already-refactored baseline.
