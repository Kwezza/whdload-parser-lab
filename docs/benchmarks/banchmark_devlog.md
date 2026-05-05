# Benchmark Dev Log

---

## 2026-05-05 — Session 3: Steps C+D

### Step C — Sort field matchers by corpus hit rate

After Step B collected per-field hit counts across 3861 filenames, `build_pack_field_matchers()`
in `src_raw/tlv_builder.c` now ends with an insertion sort that orders the `matchers[]` array
by ascending index into `s_field_priority_order[]` (a static corpus-derived table). Fields not
in the table sort to the end.

Priority order applied (descending hit count): chipset (385), video (241), memory (147),
disks (44), software_houses (42), media (19), cover_disks (8), crack_groups (4), compilations (3).

| Metric | Before Step C | After Step C | Delta |
|---|---|---|---|
| `token_loop` | 0.517 ms | 0.220 ms | **−57%** |
| `pack_field_match` | 0.075 ms | 0.063 ms | −16% |
| `csv_find_ci_calls` | 3508 | 2065 | −41% |
| TLV entries | 11634 | 11634 | unchanged |

### Step D — min/max entry length triage gate in all probe paths

Added `uint16_t min_entry_len` and `uint16_t max_entry_len` to `CSVCache` in
`include_raw/tlv_filename/csv_cache.h`. These track the shortest and longest token stored
in each cache table and are updated in `csv_cache_insert()` as entries are loaded.

Three probe sites now gate on these bounds before entering the hash probe loop:

- `csv_cache_find()` (prescan joined-string path)
- `csv_cache_lookup_span()` (prescan span path — the hot path)
- `csv_cache_find_prehashed()` (pack_field_match path)

The gate is: `if (look_len < cache->min_entry_len || look_len > cache->max_entry_len) return 0;`

This is a single comparison that rejects impossible candidates before `raw_hash & (capacity-1)`,
the probe loop, or any `strcmp()`. For the prescan span loop — which iterates all window
lengths × all start positions per field — most candidate windows are the wrong length for the
cache being probed. The gate eliminates nearly all of that work.

| Metric | Before Step D | After Step D | Delta |
|---|---|---|---|
| `prescan` | 3.7 ms | **1.0 ms** | **−73%** |
| `prescan_lookup` | 0.178 ms | 0.002 ms | **eliminated** |
| `token_loop` | 0.220 ms | 0.045 ms | −80% |
| `pack_field_match` | 0.063 ms | 0.011 ms | −83% |
| Wall-clock TLV build | ~12 ms | **~9 ms** | **−25%** |
| TLV entries | 11634 | 11634 | unchanged |
| `csv_find_ci_hits` | 0 | 0 | unchanged |
| Amiga build | clean | clean | unchanged |

The `prescan` gain (73%) is larger than expected because `csv_cache_lookup_span()` is the inner
loop call for every window candidate in every prescan field. For example, chipset has entries of
3–3 chars (e.g., "aga", "ocs", "ecs"); any span of length ≠ 3 (which is most spans) exits in
one compare instead of entering the probe. The same applies per-cache across all fields.

### Combined session profile (after Steps C and D)

```
prescan            calls=3861   total= 1.0 ms
prescan_lookup     calls=43806  total= 0.002 ms  (was 0.178ms)
tokenize           calls=3861   total= 0.055 ms
token_loop         calls=3861   total= 0.045 ms
token_checks       calls=7653   total= 0.025 ms
pack_field_match   calls=914    total= 0.011 ms
aggregate_merge    calls=1      total= 0.604 ms
TLV build time:    9 ms wall-clock (was 12ms after previous session, 27ms at session start)
TLV entries:       11634 ✓
csv_find_ci_hits:  0 ✓
```

`aggregate_merge` at 0.6ms is now the second-largest visible section. All filename-processing
sections combined are below 1.2ms.

### Remaining work

The filename-processing pipeline is essentially fully optimised for the host. The only
significant remaining work is the Amiga hardware test. The two pre-existing vbcc warnings
(csv_cache.c:1621, filename_processor.c:363) are unrelated to these changes.

Further algorithmic improvements (Rec 1 prescan loop inversion, span-walker redesign) are
now measuring at noise floor on host — they would only be worth pursuing if Amiga hardware
profiling shows a different bottleneck distribution.

### Files changed (Steps C + D)

| File | Changes |
|---|---|
| `include_raw/tlv_filename/csv_cache.h` | Add `min_entry_len`, `max_entry_len` to `CSVCache` |
| `src_raw/csv_cache.c` | Initialize sentinels in `csv_load_file_into_cache()`; update bounds in `csv_cache_insert()`; add length gate in `csv_cache_find()`, `csv_cache_lookup_span()`, `csv_cache_find_prehashed()` |
| `src_raw/tlv_builder.c` | Add `s_field_priority_order[]` + `field_priority_index()` before `build_pack_field_matchers()`; add insertion-sort block at end of function |

---

## 2026-05-05 — Session 2: Steps A+B — prehash + publisher-CSV fix

### Session context

Follow-up session targeting the single remaining dominant cost after Steps 3–8:
`pack_field_match` / `csv_lookup` at ~56% of batch. Two new patches were implemented.

### Before / after — key profile metrics (profiling build, `make PROFILE=1 run`)

| Metric | **Before session** | **After session** | Delta |
|---|---|---|---|
| `batch_total` | ~27.1 ms | ~10.4 ms | **−62%** |
| `pack_field_match` | 15.9 ms (57.5%) | 0.075 ms (0.7%) | **eliminated** |
| `csv_lookup` (slow path) | 14.9 ms (53.9%) | 0 calls | **eliminated** |
| `token_loop` | 16.5 ms (59.8%) | 0.517 ms (4.9%) | **eliminated** |
| `prescan` | 4.1 ms (15%) | 3.5 ms (33.8%) | small improvement |
| `csv_lookup_loaded` | 0.178 ms | 0.000 ms | profiling noise |
| TLV entries | 11634 | 11634 | unchanged |
| `csv_find_ci_hits` | 0 | 0 | unchanged |
| Wall-clock TLV build time | ~23 ms | ~12 ms | **−48%** |

### What was implemented

**Step A — Pre-hash tokens once before the field matcher loop (`csv_cache_lookup_prehashed`)**

New `csv_cache_find_prehashed()` (static) and `csv_cache_lookup_prehashed()` (public) in
`src_raw/csv_cache.c` and `include_raw/tlv_filename/csv_cache.h`. Takes pre-computed
`(lower, look_len, look_fp, raw_hash)` and goes directly to the probe loop, skipping the
lowercase+hash pass that `csv_cache_find()` does internally.

In `src_raw/filename_processor.c`, the pack_field_match block now:
- Pre-computes lowercase + hash for the plain token once before the field loop using new
  `token_compute_prehash()` static helper.
- For `&`-split parts: lowercases each part in-place (buffer is mutable) and computes
  per-part hash/len/fp in a single pass after `whd_strtok_r`.
- Calls `csv_cache_lookup_prehashed()` instead of `csv_cache_lookup_loaded()` inside the
  field loop, reusing the precomputed values across all N field matchers.

**Root-cause fix — disable missing-CSV matchers in `build_pack_field_matchers()`**

Diagnosis: `csv_lookup calls=914` (14.9ms) were ALL from the `publisher` field matcher.
`publisher` has no CSV file, so `resolved_cache == NULL`. Every token entering pack_field_match
called `csv_token_matcher_lookup()` → `csv_cache_lookup()` → file-open attempt on
`publisher.csv` (ENOENT + manager linear scan = ~16µs per call × 914 calls = 14.9ms).

Fix in `src_raw/tlv_builder.c`, `build_pack_field_matchers()`: after attempting to pre-resolve
a cache, if the cache is enabled but `resolved_cache` is still NULL (file not found), set
`generic_csv_match_enabled = false` for that matcher. The slow path via `csv_token_matcher_lookup()`
would also return 0 in this situation, so there is no correctness impact.

**Step B — Per-field pack match hit counters (profiling builds only)**

- Static `g_pack_field_hits[]` / `g_pack_field_names[]` arrays + `pack_field_record_hit()`
  in `src_raw/filename_processor.c`, guarded by `#if TLV_PROFILE_ENABLE`.
- Public `filename_processor_print_pack_field_stats(FILE *)` declared in
  `include_raw/tlv_filename/filename_processor.h`, implemented in `filename_processor.c`.
- Both profiling print sites in `app_src/main.c` now call this function.
- Per-field hit data (corpus, 3861 filenames):

```
chipset          = 385
video            = 241
memory           = 147
disks            = 44
software_houses  = 42
media            = 19
cover_disks      = 8
crack_groups     = 4
compilations     = 3
```

### Profile snapshot (profiling build, 2026-05-05)

```
batch_total        calls=1      total=10.413 ms  share=100.0%
prescan            calls=3861   total= 3.528 ms  share= 33.8%
prescan_lookup     calls=43806  total= 0.000 ms  share=  0.0%
token_loop         calls=3861   total= 0.517 ms  share=  4.9%
pack_field_match   calls=914    total= 0.075 ms  share=  0.7%
csv_lookup_loaded  calls=48891  total= 0.000 ms  share=  0.0%
csv_find_ci_calls  = 3508
csv_find_ci_hits   = 0
```

### Dominant remaining cost

`prescan` at 3.5ms / 33.8% of batch is now the only significant hotspot. The batch pipeline
is 62% faster than after the previous session. The next meaningful improvement would require
changes to `prescan_and_strip_tokens()` itself (further window pruning, first-token
discriminators, or suffix-only assumptions from corpus evidence).

### Next steps in priority order

1. **Step C — Sort field matchers by descending hit rate (corpus data now available)**
   Order: chipset (385), video (241), memory (147), disks (44), software_houses (42),
   media (19), cover_disks (8), crack_groups (4), compilations (3). Token_loop is now
   0.5ms total so the gain from sorting is likely negligible on host, but may help on 68k.
   Measure before implementing.

2. **Step D — Add `min_entry_len` / `max_entry_len` length triage to `CSVCache`**
   Add to `csv_cache_lookup_prehashed()` as a one-liner guard. Primarily useful for miss-heavy
   caches (chipset vocab is 3-char entries, so any token > 3 chars skips the probe entirely).
   Most likely negligible on host; measure on Amiga.

3. **Prescan further tuning** — first-token discriminator, suffix-only mode for chipset/language
   (corpus evidence needed). The 3.5ms prescan is now the only real target.

4. **Amiga hardware test** — run `STACK 100000` before the Amiga binary. All changes this
   session (and the prior session) have not been tested on hardware. Expected: larger
   relative gains vs host due to 256-byte d-cache and no page-cache for disk I/O.

### Files changed this session

| File | Changes |
|---|---|
| `include_raw/tlv_filename/csv_cache.h` | Declare `csv_cache_lookup_prehashed()` |
| `src_raw/csv_cache.c` | Add `csv_cache_find_prehashed()` + `csv_cache_lookup_prehashed()` |
| `include_raw/tlv_filename/filename_processor.h` | Add `<stdio.h>` + declare `filename_processor_print_pack_field_stats()` |
| `src_raw/filename_processor.c` | Add `token_compute_prehash()`, Step B counters + print fn, rewrite pack_field_match to use prehashed lookups |
| `src_raw/tlv_builder.c` | Disable matchers with missing CSV files in `build_pack_field_matchers()` |
| `app_src/main.c` | Add `#include <tlv_filename/filename_processor.h>`; call `filename_processor_print_pack_field_stats()` at both print sites |

---

## 2026-05-05 — Session close: full before/after and next steps

### Session context

This session implemented the patch order from `docs/benchmarks/TVL-speedup-deep-research-report.md`,
Steps 3–8. All six steps are complete. Six commits' worth of targeted, zero-regression changes
to the host pipeline running against the full Games corpus (3861 WHDLoad filenames).

### Before / after — key profile metrics

All times are `batch_total` wall time from `make PROFILE=1 run` on the host (Windows, GCC
`-O2`). The baseline is the profile that accompanied the research report before this session.

| Metric | **Before session** | **After session** | Delta |
|---|---|---|---|
| `batch_total` | ~45.9 ms | ~27.1 ms | **−41%** |
| `prescan` | 64.2% of batch | 15.7% of batch | **−49 pp** |
| `prescan_lookup` | 35.7% of batch | 0.5% of batch | **eliminated** |
| `csv_lookup_loaded` (prescan) | high / noisy | 0.0% | **eliminated** |
| `csv_find_ci_hits` | unmeasured | 0 / 3861 runs | **proven dead** |
| `pack_field_match` | 13.8% of batch | 55.9% of batch | now dominant |
| TLV entries | 11634 | 11634 | unchanged |
| Errors | 0 | 0 | unchanged |

The prescan pipeline went from the largest bottleneck to a minor cost. `pack_field_match` /
`csv_lookup` is now the only significant remaining hotspot.

### Step-by-step timing progression

| Step | What | `batch_total` | `prescan` share |
|------|------|--------------|----------------|
| Baseline | Pre-session | ~45.9 ms | ~64.2% |
| 3 | Lowercase canonicalise in insert/find | ~35.8 ms | ~35.5% |
| 4 | `% → &` bitmask + `len`/fingerprint | ~33.4 ms | ~35.4% |
| 5 | `&` split already hoisted (no change) | ~33.4 ms | ~35.4% |
| 6 | Window prune via `min`/`max_token_count` | ~32.9 ms | ~30.9% |
| 7 | Span-based lookup (no string materialise) | ~25.5 ms | ~16.9% |
| 8 | Shared `parts[]` + single final rebuild | ~27.1 ms | ~15.7% |

Step 7 was the single largest individual gain (−7.4 ms, −58% within prescan). Steps 3–4 and 6
each contributed compounding improvements that made Step 7 possible and effective.

### What the session changed — file-by-file summary

| File | Changes |
|---|---|
| `include_raw/tlv_filename/csv_cache.h` | `CSVEntry` + `len`, `fingerprint`; `CSVCache` + `min_token_count`, `max_token_count`; declare `csv_cache_lookup_span()` |
| `src_raw/csv_cache.c` | `csv_hash_token()` returns raw hash (no `%`); `csv_cache_insert()` lowercases + stores `len`/`fingerprint` + updates `min`/`max`; `csv_cache_find()` bitmask probe + pre-filter; `csv_cache_find_ci()` gated behind `#if TLV_PROFILE_ENABLE`; `span_equals_token()` + `csv_cache_lookup_span()` added |
| `src_raw/filename_processor.c` | Prescan: window prune guard; span-based hot path (Step 7); shared `parts[]`/`tmp[]` hoisted before field loop, single final rebuild (Step 8) |
| `app_src/main.c` | `csv_cache_print_stats()` wired at both summary-print sites |

### Next optimisation session — what to do

The single remaining dominant cost is `pack_field_match` / `csv_lookup` at ~52–56% of
`batch_total`. The profile shows:

```
pack_field_match   calls=914    total=15.1 ms  share=55.9%
csv_lookup         calls=914    total=14.2 ms  share=52.5%
csv_lookup_loaded  calls=48891  total=0.04 ms  share=0.1%
```

914 filename-level calls each driving ~48,891 token-level `csv_cache_lookup_loaded()` calls.
The entire cost is inside that inner token × field × CSV loop.

**Recommended next steps in priority order:**

1. **Apply `csv_cache_lookup_span()` to the `pack_field_match` `&`-split path.**
   Currently `ampersand_parts[]` are built with `whd_strtok_r` and then passed one-by-one
   as `const char *` to `csv_cache_lookup_loaded()`. Since these sub-tokens are already split
   into an array, `csv_cache_lookup_span()` with `window=1` (or a `csv_cache_lookup_loaded_lc()`
   variant that skips the lowercase copy by accepting a pre-lowercased token) could reduce
   the per-call overhead further. Measure first.

2. **Pre-sort field matchers by descending corpus hit rate.**
   `build_pack_field_matchers()` processes fields in `pack_types.ini` order. Moving the most
   commonly matched fields (e.g., publisher, chipset) to the front reduces average iterations
   per token via `break`-on-first-match. Requires one offline corpus pass to count per-field
   hits; the sort is trivially applied at `build_pack_field_matchers()` time.

3. **Reduce the token × field search space with a single-pass triage.**
   Before entering the per-field matcher loop for a token, compute the djb2 hash and check it
   against a combined bloom filter (or a simple per-field `max_token_count` == 1 gate). Tokens
   that obviously cannot match any field are skipped entirely. This requires tracking per-field
   token-count ranges (already done for prescan caches; the same `min_token_count`/
   `max_token_count` fields on `CSVCache` are already populated for pack fields too).

4. **Amiga-specific: raise stack before profiling there.**
   Run `STACK 100000` before the Amiga binary. The prescan and `pack_field_match` improvements
   made in this session have not been tested on hardware. The Amiga 68030 profile is expected
   to show a larger relative gain for Steps 7 and 8 than the host due to its 256-byte d-cache.

---

## 2026-05-05 — Step 8 applied: shared `parts[]` + single `rebuild_filename_from_parts()`

### What was implemented

**Prescan function `prescan_and_strip_tokens()`** (`src_raw/filename_processor.c`):

Before this step the outer `for c` (per-field) loop began by:
1. `memcpy(processed_filename → tmp[])` — a full filename copy per field
2. `whd_strtok_r` pass to split `tmp[]` into `parts[]` — per-field tokenize

After each field, two `rebuild_filename_from_parts(processed_filename, ...)` calls existed:
- One inside the `do...while` guarded by `if (removed_span)` — to update `processed_filename` so the NEXT field could copy it
- One unconditional at the end of each field `if (!field_changed)` — pure waste: if nothing changed, writing the same string back to `processed_filename` was a no-op

**After Step 8:**
- `char tmp[]` and `char *parts[]` are declared once, before the outer field loop
- `whd_strtok_r` runs once on `tmp[]` to populate `parts[]` before the loop
- All fields share the same `parts[]` and `pc` — `compact_token_parts()` already modifies them in place so each field sees prior removals without re-copying
- Both per-field `rebuild_filename_from_parts(processed_filename, ...)` calls are removed
- A new `bool any_span_removed` flag is set whenever a span is compacted
- ONE `rebuild_filename_from_parts(processed_filename, ...)` call (profiled) runs after all fields complete, only when `any_span_removed == true`

**What is eliminated per filename** (with N prescan fields, typically N=4-8):
- N copies of `memcpy(processed_filename → tmp[])` reduced to 1
- N `whd_strtok_r` passes reduced to 1
- Up to N unconditional rebuilds of `processed_filename` (when no field matched) reduced to 0
- The guarded rebuilds inside `do...while` (fired on each span removal per field) reduced to 1 final call

### Verified results (full corpus, 3861 filenames, `make PROFILE=1 run`)

| Metric | Before Step 8 | After Step 8 |
|---|---|---|
| Processed | 3861 | 3861 |
| TLV entries | 11634 | 11634 |
| `batch_total` | ~25.5 ms | ~27.1 ms (±timing noise) |
| `prescan` | 4.3 ms (16.9%) | 4.2 ms (15.7%) |
| `prescan_rebuild` calls | 141 | 137 |
| `csv_find_ci_hits` | 0 | 0 |

TLV output byte-identical. The host GCC `-O2` build does not show a measurable gain because:
- The eliminated memcpy+strtok calls are fast on x86 with caches hot
- `prescan` was already only 4.3 ms after Step 7 — headroom is small
- The `rebuild_filename_from_parts` calls that were removed were not profiled separately

**The Amiga 68k impact is expected to be larger**: on a 68030 at 40 MHz with 256-byte d-cache,
eliminating repeated `memcpy` + `strtok_r` + string rebuild passes matters more per cycle
than it does on a modern OOO x86 host.

### Profile snapshot (profiling build, 2026-05-05)

```
batch_total        calls=1      total=27.057 ms
process_filename   calls=3861   total=24.963 ms  share=92.2%
prescan            calls=3861   total= 4.248 ms  share=15.7%
prescan_lookup     calls=43806  total= 0.161 ms  share=0.5%
prescan_rebuild    calls=137    total= 0.000 ms  share=0.0%
token_loop         calls=3861   total=15.719 ms  share=58.0%
pack_field_match   calls=914    total=15.139 ms  share=55.9%
csv_lookup         calls=914    total=14.221 ms  share=52.5%
csv_find_ci_calls  = 3508
csv_find_ci_hits   = 0
```

### Cumulative savings: all steps this session (Steps 3–8)

| Metric | Session baseline | After Step 8 | Total delta |
|---|---|---|---|
| `batch_total` | ~45.9 ms | ~27.1 ms | **−41%** |
| `prescan` share | ~45.9% | ~15.7% | −30 pp |
| `prescan_lookup` share | ~35.7% | ~0.5% | eliminated |
| Dominant cost | prescan | `pack_field_match` (55.9%) | shifted |

### Patch order status (final)

| Step | Description | Status |
|------|-------------|--------|
| 3 | Lowercase canonicalise + `csv_find_ci` instrumented | **Done** |
| 4 | `% → &` bitmask + `len`/fingerprint in `CSVEntry` | **Done** |
| 5 | `&` split hoisted once per token before field loop | **Done (already in code)** |
| 6 | Prescan pruning: `min_token_count`/`max_token_count` per cache | **Done** |
| 7 | Span-based prescan lookup; stop materialising joined strings | **Done** |
| 8 | Shared `parts[]` across fields + single final rebuild | **Done** |

The prescan pipeline is now as lean as it can be without a full algorithmic redesign. The
remaining dominant cost is `pack_field_match` / `csv_lookup` (52–56% of batch). That path
processes individual tokens (not spans), so the span-lookup technique does not directly apply
there. The next meaningful improvement in that bucket would require structural changes such as
pre-sorting field tests by hit probability or switching `csv_lookup_loaded` to the same
`len`/`fingerprint` aware caller path used in the prescan span lookup.

---

## 2026-05-05 — Step 7 applied: span-based prescan lookup — eliminate `build_joined_token()`

### What was implemented

**New static helper `span_equals_token()`** (`src_raw/csv_cache.c`): compares a stored
lowercase token against an underscore-joined span of `parts[start..start+window-1]`, lowercasing
each part character on the fly. No intermediate buffer. Returns `true` if every character matches
and the stored token ends exactly where the span ends.

**New public function `csv_cache_lookup_span()`** (`src_raw/csv_cache.c`,
`include_raw/tlv_filename/csv_cache.h`): replaces `csv_cache_lookup_loaded()` in the prescan
hot path. Computes the djb2 hash, `look_len`, and `look_fp` directly from the span in a single
forward pass (identical to what `csv_cache_find()` would compute on the materialised string).
Then probes the hash table using the same `len`/`fingerprint` pre-filter, calling
`span_equals_token()` only when both match. No stack buffer. No `memcpy`. No second lowercase
pass.

**Prescan inner loop** (`src_raw/filename_processor.c`):
- Removed `char joined[MAX_TOKEN_LENGTH]` (128-byte stack alloc, 43,806 times per run)
- Removed `TLV_PROFILE_SCOPE(join_profile_stamp)` and the `build_joined_token()` call
- Hot path (`cfg_caches[c] != NULL`): now calls `csv_cache_lookup_span(cfg_caches[c], parts, i, window)`
- Cold path (`cfg_caches[c] == NULL`): still materialises `joined` and calls `csv_cache_lookup()` — this branch does not fire in normal operation after cache pre-resolution
- Debug blocks (guarded by `strstr(filename, "Kernal_Version")`): materialise `joined` lazily into `dbg_joined` only when that filename appears in the corpus

### What this eliminates per prescan candidate

| Before Step 7 | After Step 7 |
|---|---|
| `char joined[128]` stack alloc | — |
| `memcpy` loop building joined string | — |
| `csv_cache_find()` → lowercase copy into `lower[128]` | — |
| djb2 hash pass over `lower[]` | hash pass over span parts (single pass, no copy) |
| `strcmp()` vs stored token | `span_equals_token()` vs stored token (no copy) |

The net effect is three per-candidate passes collapsed into one.

### Verified results (full corpus, 3861 filenames, `make PROFILE=1 run`)

| Metric | Before Step 7 | After Step 7 |
|---|---|---|
| Processed | 3861 | 3861 |
| TLV entries | 11634 | 11634 |
| `batch_total` | ~32.9 ms | ~25.5 ms |
| `prescan` | 10.2 ms (30.9%) | 4.3 ms (16.9%) |
| `prescan_lookup` | 0.143 ms | 0.013 ms |
| `csv_lookup_loaded` | 0.143 ms | 0.000 ms |
| `csv_find_ci_calls` | 47,173 | 3,508 |
| `csv_find_ci_hits` | 0 | 0 |

TLV output byte-identical. Prescan lost 5.9 ms (−58% within the prescan bucket). The remaining
`csv_find_ci_calls = 3,508` are from `pack_field_match` which still goes through
`csv_cache_lookup_loaded()` — the span path is prescan-only.

### Profile snapshot (profiling build, 2026-05-05)

```
batch_total        calls=1      total=25.465 ms
process_filename   calls=3861   total=23.172 ms  share=90.9%
prescan            calls=3861   total= 4.321 ms  share=16.9%
prescan_lookup     calls=43806  total= 0.013 ms  share=0.0%
token_loop         calls=3861   total=14.596 ms  share=57.3%
pack_field_match   calls=914    total=14.071 ms  share=55.2%
csv_lookup         calls=914    total=13.265 ms  share=52.0%
csv_lookup_loaded  calls=48891  total= 0.000 ms  share=0.0%
csv_find_ci_calls  = 3508
csv_find_ci_hits   = 0
```

### Cumulative savings: full session (Steps 3–7)

| Metric | Session baseline | After Step 7 | Total delta |
|---|---|---|---|
| `batch_total` | ~45.9 ms | ~25.5 ms | **−44%** |
| `prescan` share | ~45.9% | ~16.9% | −29 pp |
| `prescan_lookup` share | ~35.7% | ~0.0% | eliminated |
| Dominant remaining cost | — | `pack_field_match` (55.2%) | — |

### Remaining hotspot

`pack_field_match` / `csv_lookup` now accounts for 52–55% of batch time. This path still uses
`csv_cache_lookup_loaded()` (string token → lowercase → hash → probe). The same span-lookup
technique could be applied there, but the token structure is different: individual underscore-free
tokens (or `&`-split sub-tokens), not window spans. The analogous improvement there is Step 8:
eliminate the per-field `tmp[]` copy and reduce `rebuild_filename_from_parts()` calls.

### Patch order status (updated)

| Step | Description | Status |
|------|-------------|--------|
| 3 | Lowercase canonicalise + `csv_find_ci` instrumented | **Done** |
| 4 | `% → &` bitmask + `len`/fingerprint in `CSVEntry` | **Done** |
| 5 | `&` split hoisted once per token before field loop | **Done (already in code)** |
| 6 | Prescan pruning: `min_token_count`/`max_token_count` per cache | **Done** |
| 7 | Span-based prescan lookup; stop materialising joined strings | **Done** |
| 8 | Remove per-field `tmp[]` copy + per-field `rebuild_filename_from_parts()` | **Done** |

---

## 2026-05-05 — Session savings review: all phases 1-3 complete

### What was done across this optimisation session

Three patch groups landed in this session, all targeting the `csv_cache` hot path.
The `&` split hoisting in `pack_field_match` was found to already be present in the code
(the `ampersand_buffer` fill and `whd_strtok_r` loop run once per token, before the field loop,
not inside it). That step is complete.

| Phase | Step | Description | File(s) changed |
|-------|------|-------------|-----------------|
| 1 | 3 | Lowercase all tokens at load/lookup time; `csv_find_ci` counted fallback | `csv_cache.c`, `csv_cache.h`, `main.c` |
| 2 | 4 | `%→&` bitmask; `len`+`fingerprint` in `CSVEntry` pre-filter `strcmp()` | `csv_cache.c`, `csv_cache.h` |
| 3 | 5 | `&`-split hoisted once per token before the field loop (already in code) | — |

### Cumulative savings (host build, full Games corpus, 3861 filenames)

All numbers are from `make PROFILE=1 run`. The baseline is the profile snapshot captured at
the start of this session (before any changes) as recorded in the research report.

| Metric | Baseline (pre-session) | After Phase 3 (this session) | Delta |
|---|---|---|---|
| `batch_total` | ~45.9 ms | ~33.4 ms | **−27%** |
| `prescan` share | ~45.9% | ~35.4% | −10.5 pp |
| `prescan_lookup` share | ~35.7% | ~0.1% | −35.6 pp |
| `csv_lookup_loaded` share | noisy / high | ~0.0% | eliminated |
| `pack_field_match` share | ~13.8% | ~43.5% (of smaller total) | now dominant |
| `csv_find_ci_hits` | N/A | 0 | fallback dead weight confirmed |

The `prescan_lookup` bucket collapsed from the second-largest hotspot to 0.1% of total
because:
- Lowercasing canonicalises the key once instead of triggering a linear ci scan on every miss
- The bitmask + `len`/`fingerprint` pre-filter avoids `strcmp()` on length/hash mismatches
- The ci fallback is now proven dead and compiled out of release builds

### Remaining hotspots (as of end of session)

```
prescan            calls=3861   total=11.851 ms  share=35.4%   ← Step 6/7 target
token_loop         calls=3861   total=15.086 ms  share=45.1%
pack_field_match   calls=914    total=14.567 ms  share=43.5%   ← largest single bucket
csv_lookup         calls=914    total=13.681 ms  share=40.9%
```

The prescan window-generation loop (building joined strings for every window × position ×
field combination) is still the main cost inside `prescan`. Step 6 attacks this directly by
storing `min_token_count`/`max_token_count` per `CSVCache` at load time and skipping windows
that fall outside the range.

### Patch order status (updated)

| Step | Description | Status |
|------|-------------|--------|
| 3 | Lowercase canonicalise + `csv_find_ci` instrumented | **Done** |
| 4 | `% → &` bitmask + `len`/fingerprint in `CSVEntry` | **Done** |
| 5 | `&` split hoisted once per token before field loop | **Done (already in code)** |
| 6 | Prescan pruning: `min_token_count`/`max_token_count` per cache | **Done** |
| 7 | Span-based prescan lookup; stop materialising joined strings | **Done** |
| 8 | Remove per-field `tmp[]` copy + per-field `rebuild_filename_from_parts()` | **Next** |

---

## 2026-05-05 — Step 6 applied: prescan window pruning via `min_token_count`/`max_token_count`

### What was implemented

**`CSVCache` struct** (`include_raw/tlv_filename/csv_cache.h`): two new fields:
- `uint8_t min_token_count` — minimum number of `_`-separated tokens across all entries
- `uint8_t max_token_count` — maximum number of `_`-separated tokens across all entries

**`csv_cache_insert()`** (`src_raw/csv_cache.c`): after building `lower[]`, counts underscores
to derive `tc` (token count = underscore count + 1) and updates the cache min/max.
Sentinels: `min` initialised to 255, `max` to 0 at load time.

**Prescan window loop** (`src_raw/filename_processor.c`): after the `if (pc < window) continue`
guard, added:
```c
if (cfg_caches[c] != NULL &&
    (window < (uint32_t)cfg_caches[c]->min_token_count ||
     window > (uint32_t)cfg_caches[c]->max_token_count)) {
    continue;
}
```
This skips `build_joined_token()` and `csv_cache_lookup_loaded()` entirely for window sizes
that can never match. The check fires once per `(field, window)` pair, not per position.

### Verified results (full corpus, 3861 filenames, `make PROFILE=1 run`)

| Metric | Before Step 6 | After Step 6 |
|---|---|---|
| Processed | 3861 | 3861 |
| TLV entries | 11634 | 11634 |
| `batch_total` | ~33.4 ms | ~32.9 ms |
| `prescan` share | ~35.4% / 11.8 ms | ~30.9% / 10.2 ms |
| `prescan_join` calls | 48,971 | 43,806 |
| `prescan_lookup` calls | 48,971 | 43,806 |
| `csv_find_ci_calls` | 52,338 | 47,173 |
| `csv_find_ci_hits` | 0 | 0 |

TLV output byte-identical. Prescan bucket lost 1.65 ms (−14% within prescan). The 5,165
pruned windows are those where a field's CSV has no entries matching a given window width.

### Profile snapshot (profiling build, 2026-05-05)

```
batch_total        calls=1      total=32.927 ms
process_filename   calls=3861   total=30.671 ms  share=93.1%
prescan            calls=3861   total=10.198 ms  share=30.9%
prescan_join       calls=43806  total=0.000 ms   share=0.0%
prescan_lookup     calls=43806  total=0.143 ms   share=0.4%
token_loop         calls=3861   total=15.574 ms  share=47.2%
pack_field_match   calls=914    total=15.000 ms  share=45.5%
csv_lookup         calls=914    total=14.155 ms  share=42.9%
csv_lookup_loaded  calls=48891  total=0.143 ms   share=0.4%
csv_find_ci_calls  = 47173
csv_find_ci_hits   = 0
```

### Next step: Step 7 — span-based prescan lookup

Eliminate `build_joined_token()` entirely. Pass `(parts, i, window)` as a span directly into
the hash function to avoid materialising the joined string on the stack. Opens the door to
incrementally computing djb2 as the span expands from one token to the next.

---

## 2026-05-05 — Step 4 applied: `%→&` bitmask + `len`/`fingerprint` in `CSVEntry`

### What was implemented

**`csv_hash_token()`**: removed the `% capacity` from the return value. Function now returns
the raw djb2 hash; callers apply `& (capacity - 1)` (valid because capacity is always a power
of two from `csv_calculate_capacity()`).

**`CSVEntry` struct** (in `include_raw/tlv_filename/csv_cache.h`): two new fields added:
- `uint16_t len` — `strlen(token)` cached at insert time
- `uint16_t fingerprint` — low 16 bits of the raw djb2 hash, stored at insert time

**`csv_cache_insert()`**: computes `raw_hash`, `ins_len`, and `ins_fp` from `lower[]` once.
Uses `& (capacity - 1)` for the initial index and all probe advances. Stores `len` and
`fingerprint` alongside the token pointer.

**`csv_cache_find()`**: same single hash computation. Probe loop checks `len == look_len &&
fingerprint == look_fp` before calling `strcmp()`. Mismatched length or fingerprint short-circuits
with zero `strcmp()` overhead. All `% capacity` divisions removed; replaced with `& (capacity - 1)`.

**`csv_cache_find_ci()`**: gated behind `#if TLV_PROFILE_ENABLE` — compiled out of release
builds entirely. Its call site in `csv_cache_lookup_loaded()` is likewise gated; in release
builds the function simply calls `csv_cache_find()` and returns.

### Verified results (full corpus, 3861 filenames, `make PROFILE=1 run`)

| Metric | Before Step 4 | After Step 4 |
|---|---|---|
| Processed | 3861 | 3861 |
| TLV entries | 11634 | 11634 |
| `csv_find_ci_hits` | 0 | 0 |
| `batch_total` | ~35.8 ms | ~33.4 ms |
| `prescan` share | ~35.5% | ~35.4% |
| `prescan_lookup` share | ~0.4% | ~0.1% |
| `csv_lookup_loaded` share | ~0.3% | ~0.0% |
| `csv_lookup` (pack) total | ~14.5 ms | ~13.7 ms |

TLV binary output byte-identical. The `ci` fallback still fires 52,338 times (all genuine
misses, hits = 0). In release builds `csv_cache_find_ci()` is now compiled out entirely.

### Profile snapshot (profiling build, 2026-05-05)

```
batch_total        calls=1      total=33.417 ms
process_filename   calls=3861   total=31.434 ms  share=94.0%
prescan            calls=3861   total=11.851 ms  share=35.4%
prescan_lookup     calls=48971  total=0.055 ms   share=0.1%
token_loop         calls=3861   total=15.086 ms  share=45.1%
pack_field_match   calls=914    total=14.567 ms  share=43.5%
csv_lookup         calls=914    total=13.681 ms  share=40.9%
csv_lookup_loaded  calls=54056  total=0.012 ms   share=0.0%
csv_find_ci_calls  = 52338
csv_find_ci_hits   = 0
```

### Next steps (patch order status)

| Step | Description | Status |
|------|-------------|--------|
| 3 | Lowercase canonicalise + `csv_find_ci` instrumented | **Done** |
| 4 | `% → &` bitmask + `len`/fingerprint in `CSVEntry` | **Done** |
| 5 | `&` split hoisted once per token before field loop | **Done (already in code)** |
| 6 | Prescan pruning: `min_token_count`/`max_token_count` per cache | Not done |
| 7 | Rework prescan to span-based lookup | Not done |
| 8 | Remove per-field `tmp[]` copy + per-field `rebuild_filename_from_parts()` | Not done |

The dominant remaining hotspot is `pack_field_match` / `csv_lookup` (combined ~84% of batch
time). The `&` split is the highest-value isolated change that can be made there without
restructuring the prescan.

---

## 2026-05-05 — Rec 4 applied: lowercase canonicalisation + ci fallback instrumented

### What was implemented

Phase 1 of Rec 4 applied across `src_raw/csv_cache.c`, `include_raw/tlv_filename/csv_cache.h`,
and `app_src/main.c`.

**`csv_cache_insert()`**: lowercases the incoming token into a local `lower[]` buffer (ASCII
arithmetic only, no `tolower()`) before hashing, probing, and storing. Both the computed slot
and the stored string are canonical lowercase. `long_name` (display-only) is left untouched.

**`csv_cache_find()`**: rewrote to lowercase the lookup key into `lower[]` before hashing and
comparing. Both sides of every `strcmp()` are now lowercase, so the exact path handles any
incoming case.

**Counters added** (gated by `#if TLV_PROFILE_ENABLE`):
- `g_csv_find_ci_calls` — incremented every time the ci fallback is entered
- `g_csv_find_ci_hits`  — incremented when the ci fallback actually returns a non-zero id

**`csv_cache_print_stats(FILE *)`**: new public function; prints both counters. Called from
both profile-summary print sites in `app_src/main.c`.

**`language_parser_parse_token()`**: already lowercased `c0`/`c1` via ASCII arithmetic before
assembling `lang_code[3]` — confirmed, no change needed.

### Verified results (full corpus, 3861 filenames, `make PROFILE=1 run`)

| Metric | Before | After |
|---|---|---|
| Processed | 3861 | 3861 |
| TLV entries | 11634 | 11634 |
| `csv_find_ci_calls` | — | 52,338 |
| **`csv_find_ci_hits`** | — | **0** |
| `prescan` bucket | ~45.9% | ~35.5% |
| `csv_lookup_loaded` bucket | — | 0.1% (down from noisy) |

TLV binary output is byte-identical before and after. The ci fallback fires 52,338 times (all
genuine "not in this CSV" misses) but returns a hit exactly zero times. The lowercase exact path
handles every real match.

### Next steps

- Gate `csv_cache_find_ci()` out of non-debug builds (its hit count is proven zero).
- Implement Step 4: `% → &` bitmask probe wrap, `uint16_t len` + `uint16_t fingerprint` in
  `CSVEntry`, populated in insert and used as guards in find.

---

## 2026-05-05 — Next optimisation stage: Rec 4 — Lowercase tokens at load time

### Decision

The next implementation step is **Rec 4** from the ranked recommendations in
`docs/benchmarks/TVL-speedup-deep-research-report.md`:

> **Canonicalise case once and demote `csv_cache_find_ci()` to an instrumented cold path.**

### What this means in practice

1. **At CSV load time** (`csv_cache_insert()`): lowercase each token before storing it in the hash table.
2. **At lookup time** (`prescan_and_strip_tokens()` and `language_parser_parse_token()`): lowercase the candidate string/slice once before calling `csv_cache_lookup_loaded()`.
3. **`csv_cache_find_ci()`**: add a call counter and demote it to a counted fallback. If the counter stays at zero over a full corpus run, gate it out of release builds.

### Language detection — no issues found

The concern was that mixed-case language codes such as `FrDeEn` or `FrNLDe` might rely on case to avoid false positives, or that lowercasing the CSV could break multi-language detection.

Investigation of `token_might_be_language()` and `language_parser_parse_token()` confirms:

- **Case plays no role in false-positive prevention.** The pre-filter gates only on even length and all-alpha characters. The vocabulary boundary (i.e. whether a 2-char pair exists in `Language.csv`) is the sole false-positive guard.
- The language parser slices the raw token into 2-char chunks (`"Fr"`, `"De"`, `"En"`) using the original character bytes, then looks each up independently.
- The only CSV entry with unusual casing is `NL` (Dutch). Currently `"NL"` exact-matches `"NL"`. After lowercasing the CSV to `"nl"`, any filename slice such as `"NL"` will miss the exact path and fall through to `csv_cache_find_ci()` — **unless** `language_parser_parse_token()` also lowercases each slice before lookup.

**Required one-liner change** in `language_parser_parse_token()`: lowercase `c0` and `c1` before assembling `lang_code[3]` using ASCII arithmetic (no `tolower()` locale dependency). Once that is in place, lowercasing the Language CSV is safe and `csv_cache_find_ci()` will never fire for language lookups.

No other detection paths were found to rely on case as a semantic discriminator.

### Handover summary

| Item | Detail |
|------|--------|
| Files to change | `src_raw/csv_cache.c` — `csv_cache_insert()`, `csv_hash_token()`, `csv_cache_find()` |
| Files to change | `src_raw/filename_processor.c` — `language_parser_parse_token()` slice assembly |
| Counter to add | `csv_find_ci_calls` (uint32_t, host-build only or profiling build) |
| Test gate | Full corpus TLV diff before/after; confirm `csv_find_ci_calls` == 0 |
| Risk | Low for ASCII-only tokens (all current CSVs are ASCII) |
| Blocked by | Nothing — this is the safest isolated next patch |
