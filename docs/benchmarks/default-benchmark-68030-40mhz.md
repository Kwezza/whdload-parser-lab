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

The pre-refactoring baseline (2026-05-01) and the post-refactoring result (2026-05-06) are both preserved below. The post-refactoring run is the active default baseline.

### Post-refactoring baseline (2026-05-06) — active default

Profiled run summary after all optimizations (Steps 3–8, A, B, C, D), from `benchmark-summary.txt`:

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
TLV build time: 97420 ms
TLV save time: 1500 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=     120.000 ms avg=     120.000 ms max=     120.000 ms share=  0.1%
batch_total        calls=1      total=   90280.000 ms avg=   90280.000 ms max=   90280.000 ms share=100.0%
record_init        calls=3861   total=    1040.000 ms avg=       0.269 ms max=      20.000 ms share=  1.1%
process_filename   calls=3861   total=   85860.000 ms avg=      22.237 ms max=      60.000 ms share= 95.1%
sanitize           calls=3861   total=    1020.000 ms avg=       0.264 ms max=      20.000 ms share=  1.1%
prescan            calls=3861   total=   55420.000 ms avg=      14.353 ms max=      40.000 ms share= 61.3%
prescan_lookup     calls=43806  total=   35140.000 ms avg=       0.802 ms max=      20.000 ms share= 38.9%
prescan_rebuild    calls=137    total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
tokenize           calls=3861   total=    4480.000 ms avg=       1.160 ms max=      20.000 ms share=  4.9%
token_loop         calls=3861   total=   14700.000 ms avg=       3.807 ms max=      20.000 ms share= 16.2%
token_checks       calls=7653   total=    8260.000 ms avg=       1.079 ms max=      20.000 ms share=  9.1%
pack_field_match   calls=914    total=    2560.000 ms avg=       2.800 ms max=      20.000 ms share=  2.8%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup_loaded  calls=47448  total=   14560.000 ms avg=       0.306 ms max=      20.000 ms share= 16.1%
tlv_add_entry      calls=23268  total=    6040.000 ms avg=       0.259 ms max=      40.000 ms share=  6.6%
aggregate_merge    calls=1      total=    6640.000 ms avg=    6640.000 ms max=    6640.000 ms share=  7.3%
csv_find_ci_calls           = 2065
csv_find_ci_hits            = 0
```

### Pre-refactoring baseline (2026-05-01) — reference

Original profiled run summary before optimization work:

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

---

## Refactoring Results — 68030 @ 40MHz Amiga Hardware

Two profiled runs were recorded on the physical 68030 Amiga hardware during the refactoring session (2026-05-06). Both are preserved from `benchmark-summary.txt`.

### Intermediate Amiga run — after prescan work, before Steps A/C/D

This run was taken after the early prescan span optimizations (Steps 3–8) but before the pre-hash, field-sort, and min/max-length-gate patches were applied. The publisher.csv null-matcher overhead is already partially reduced compared to the pre-refactoring baseline (`csv_lookup` drops from 60758 calls to 914 calls).

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
TLV build time: 160540 ms
TLV save time: 1380 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=     120.000 ms avg=     120.000 ms max=     120.000 ms share=  0.0%
batch_total        calls=1      total=  153380.000 ms avg=  153380.000 ms max=  153380.000 ms share=100.0%
record_init        calls=3861   total=    1240.000 ms avg=       0.321 ms max=      20.000 ms share=  0.8%
process_filename   calls=3861   total=  147940.000 ms avg=      38.316 ms max=     160.000 ms share= 96.4%
sanitize           calls=3861   total=    1220.000 ms avg=       0.315 ms max=      20.000 ms share=  0.7%
prescan            calls=3861   total=  108020.000 ms avg=      27.977 ms max=     100.000 ms share= 70.4%
prescan_join       calls=48971  total=   12420.000 ms avg=       0.253 ms max=      20.000 ms share=  8.0%
prescan_lookup     calls=48971  total=   58580.000 ms avg=       1.196 ms max=      20.000 ms share= 38.1%
prescan_rebuild    calls=141    total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
tokenize           calls=3861   total=    4120.000 ms avg=       1.067 ms max=      20.000 ms share=  2.6%
token_loop         calls=3861   total=   23520.000 ms avg=       6.091 ms max=      60.000 ms share= 15.3%
token_checks       calls=7653   total=    7540.000 ms avg=       0.985 ms max=      20.000 ms share=  4.9%
pack_field_match   calls=914    total=   12160.000 ms avg=      13.304 ms max=      20.000 ms share=  7.9%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup         calls=914    total=    7120.000 ms avg=       7.789 ms max=      20.000 ms share=  4.6%
csv_lookup_loaded  calls=54056  total=   37440.000 ms avg=       0.692 ms max=      20.000 ms share= 24.4%
tlv_add_entry      calls=23268  total=    6680.000 ms avg=       0.287 ms max=      20.000 ms share=  4.3%
aggregate_merge    calls=1      total=    6620.000 ms avg=    6620.000 ms max=    6620.000 ms share=  4.3%
```

### Before / after comparison — key 68030 profiled metrics

| Metric | Pre-refactoring (2026-05-01) | Intermediate | Post-refactoring (2026-05-06) | Net delta |
|---|---|---|---|---|
| TLV build time | 166720 ms | 160540 ms | **97420 ms** | **−41.6%** |
| batch_total | 160440 ms | 153380 ms | 90280 ms | −43.7% |
| process_filename | 157400 ms (98.1%) | 147940 ms (96.4%) | 85860 ms (95.1%) | −45.5% |
| prescan | 103040 ms (64.2%) | 108020 ms (70.4%) | 55420 ms (61.3%) | −46.2% |
| prescan_lookup | 57380 ms (35.7%) | 58580 ms (38.1%) | 35140 ms (38.9%) | −38.8% |
| prescan_join | 12980 ms (8.0%) | 12420 ms (8.0%) | — (eliminated) | eliminated |
| token_loop | 38300 ms (23.8%) | 23520 ms (15.3%) | 14700 ms (16.2%) | −61.6% |
| pack_field_match | 22220 ms (13.8%) | 12160 ms (7.9%) | 2560 ms (2.8%) | **−88.5%** |
| csv_lookup (slow path) | 52480 ms (32.7%) | 7120 ms (4.6%) | — (eliminated) | eliminated |
| csv_lookup_loaded | n/a | 37440 ms (24.4%) | 14560 ms (16.1%) | — |
| aggregate_merge | 6280 ms (3.9%) | 6620 ms (4.3%) | 6640 ms (7.3%) | stable |
| TLV entries | 11634 | 11634 | 11634 | unchanged |
| Errors | 0 | 0 | 0 | unchanged |

Key observations:

- Overall build time cut by 42% with no correctness regressions.
- `pack_field_match` reduced by 88.5% — the prehash + field-sort + min/max-length-gate patches combined eliminated nearly all of the per-token CSV field matching overhead.
- The slow-path `csv_lookup` (publisher.csv open attempts at ~16 µs each × 914 calls) is now fully eliminated.
- `prescan_join` is gone — the shared `parts[]` restructure (Step 8) removed the per-field `memcpy` + `whd_strtok_r` passes that produced this metric.
- `prescan` dropped by 46% in absolute time. It is still the dominant cost at 61.3% of batch.
- `aggregate_merge` (6640 ms, 7.3% of batch) is now the second-largest non-prescan item and is worth tracking in future sessions.

## Additional Comparison Benchmark - 68000 @ 7MHz

Comparison benchmark environment:

- CPU: 68000 @ 7MHz
- Runtime: **Real hardware** (no CPU cache, no Fast RAM)
- Chip RAM: 2 MB
- Fast RAM: none
- Input DAT: `assets_raw/Games(19-05-2025).dat`
- Output TLV: `output/Games(19-05-2025).tlv`
- CSV folder: `assets_raw/defs`
- Pack types: `assets_raw/prefs/pack_types.ini`

This machine has no CPU cache of any kind. Every instruction fetch and every data access goes
to chip RAM. This makes it the most memory-latency-sensitive environment available and a
useful counterpoint to the 68030 numbers, which benefit from a 256-byte data cache.

### Post-refactoring profiled run (2026-05-06) — active

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
TLV build time: 591640 ms
TLV save time: 7020 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=     540.000 ms avg=     540.000 ms max=     540.000 ms share=  0.0%
batch_total        calls=1      total=  549380.000 ms avg=  549380.000 ms max=  549380.000 ms share=100.0%
record_init        calls=3861   total=    7600.000 ms avg=       1.968 ms max=      20.000 ms share=  1.3%
process_filename   calls=3861   total=  507260.000 ms avg=     131.380 ms max=     340.000 ms share= 92.3%
sanitize           calls=3861   total=    6900.000 ms avg=       1.787 ms max=      20.000 ms share=  1.2%
prescan            calls=3861   total=  331620.000 ms avg=      85.889 ms max=     220.000 ms share= 60.3%
prescan_lookup     calls=43806  total=  211720.000 ms avg=       4.833 ms max=      40.000 ms share= 38.5%
prescan_rebuild    calls=137    total=     280.000 ms avg=       2.043 ms max=      20.000 ms share=  0.0%
tokenize           calls=3861   total=   20020.000 ms avg=       5.185 ms max=      40.000 ms share=  3.6%
token_loop         calls=3861   total=   84020.000 ms avg=      21.761 ms max=     120.000 ms share= 15.2%
token_checks       calls=7653   total=   46780.000 ms avg=       6.112 ms max=      40.000 ms share=  8.5%
pack_field_match   calls=914    total=   15140.000 ms avg=      16.564 ms max=     100.000 ms share=  2.7%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup_loaded  calls=47448  total=   81300.000 ms avg=       1.713 ms max=      20.000 ms share= 14.7%
tlv_add_entry      calls=23268  total=   40200.000 ms avg=       1.727 ms max=     160.000 ms share=  7.3%
aggregate_merge    calls=1      total=   40560.000 ms avg=   40560.000 ms max=   40560.000 ms share=  7.3%
csv_find_ci_calls           = 2065
csv_find_ci_hits            = 0
```

### Pre-refactoring profiled run (pre-2026-05-05) — reference

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
TLV build time: 306240 ms
TLV save time: 7140 ms
```

### 68000 before / after refactoring comparison

| Metric | Pre-refactoring | Post-refactoring | Net delta |
|---|---|---|---|
| TLV build time | 931580 ms | **591640 ms** | **−36.5%** |
| batch_total | 892600 ms | 549380 ms | −38.5% |
| process_filename | 874160 ms (97.9%) | 507260 ms (92.3%) | −42.0% |
| prescan | 609900 ms (68.3%) | 331620 ms (60.3%) | −45.6% |
| prescan_join | 77820 ms (8.7%) | — (eliminated) | eliminated |
| prescan_lookup | 333200 ms (37.3%) | 211720 ms (38.5%) | −36.5% |
| token_loop | 176800 ms (19.8%) | 84020 ms (15.2%) | −52.5% |
| pack_field_match | 81900 ms (9.1%) | 15140 ms (2.7%) | **−81.5%** |
| csv_lookup (slow path) | 249860 ms (27.9%) | — (eliminated) | eliminated |
| csv_lookup_loaded | n/a | 81300 ms (14.7%) | — |
| aggregate_merge | 38980 ms (4.3%) | 40560 ms (7.3%) | +1580 ms (stable) |
| tlv_add_entry | 38180 ms (4.2%) | 40200 ms (7.3%) | +2020 ms (stable) |
| TLV entries | 11634 | 11634 | unchanged |
| Errors | 0 | 0 | unchanged |

Key observations:

- Build time improved by 36.5% on real 68000 hardware.
- `pack_field_match` fell by 81.5% — same optimizations that worked on 68030 translated well to 68000.
- `prescan_join` and the slow-path `csv_lookup` (publisher.csv open attempts) are eliminated.
- `prescan` absolute time halved (609900 → 331620 ms). The hotspot distribution (prescan at ~60–68% of batch) is consistent across both machines and both generations of the code.
- `aggregate_merge` and `tlv_add_entry` are nearly flat in absolute time across both runs (~38–40 seconds each). While every other section scaled down with the optimizations, these two did not — they are not CPU-bound character-work and are not sensitive to the hash or lookup improvements. They reflect memory-write-heavy operations (record building, list merging) where chip RAM latency dominates.

### Profiling overhead on 68000

Before refactoring, the non-profiled baseline was `306240 ms` and the profiled run was
`931580 ms` — a `3.0x` slowdown from instrumentation.

After refactoring, the non-profiled baseline has not yet been re-measured, but the profiled
run is `591640 ms`. If instrumentation overhead scales proportionally to the CPU work
remaining, the non-profiled post-refactoring run should be in the range of **190–210 seconds**
(roughly 591640 / 3.0). A fresh non-profiled build on real 68000 hardware would confirm this.

This matters when interpreting Amiga results:

- the profiled run is useful for relative hotspot ranking
- the profiled run substantially overstates real user-facing throughput on 68000
- the non-profiled baseline remains the better reference for "how long will this actually take"

### Hotspot pattern on the 68000 (post-refactoring)

The same broad pattern applies as on the 68030:

- `process_filename` dominates at 92.3%
- `prescan` is the largest internal hotspot at 60.3%
- `prescan_lookup` is the largest named prescan sub-cost at 38.5%
- `token_loop` is the next significant bucket at 15.2%
- `csv_lookup_loaded` at 14.7% reflects the hash probe cost for both prescan and token-loop paths
- `aggregate_merge` and `tlv_add_entry` are each 7.3% — disproportionately large compared to the 68030 (where they are 7.3% and 6.6% respectively, but the absolute times are 6x smaller)

The notable difference from 68030 is that `aggregate_merge` and `tlv_add_entry` hold a much
larger share of total time on 68000 because the chip RAM latency penalty on pointer-intensive
operations is separate from and additive to the clock-speed difference.

## What This Benchmark Shows

All numbers below reflect the post-refactoring baseline (97420 ms build time, 2026-05-06) unless noted otherwise.

### 1. Save time is not the problem

The TLV save phase is only `1500 ms`, while TLV build time is `97420 ms`.

This remains true after the refactoring. Optimization effort should stay focused on the in-memory conversion pipeline.

### 2. Filename processing still dominates the build

The largest top-level bucket is:

- `process_filename = 85860 ms`
- `process_filename share = 95.1%`

Nearly the entire build cost remains inside the per-filename processing path.

### 3. Prescan is still the largest hotspot

The largest internal bucket is:

- `prescan = 55420 ms`
- `prescan share = 61.3%`

Prescan dropped by 46% in absolute terms (from 103040 ms), but remains the highest-priority optimization target.

### 4. The biggest prescan cost is still lookup work

Within prescan:

- `prescan_lookup = 35140 ms` (38.9% of batch)
- `prescan_rebuild = 0 ms`

`prescan_join` has been eliminated by the shared `parts[]` restructure (Step 8). The remaining prescan overhead is split between the hash probe loops (`prescan_lookup`) and string/window management work (~20280 ms unaccounted in sub-metrics).

### 5. The token loop is now a secondary cost

The next major bucket is:

- `token_loop = 14700 ms`
- `token_loop share = 16.2%`

Within the token loop:

- `token_checks = 8260 ms` (9.1%)
- `pack_field_match = 2560 ms` (2.8%)
- `unknown_token = 0 ms`

`pack_field_match` fell by 88.5% (from 22220 ms) following the prehash, field-sort, and min/max-length-gate patches. `token_checks` is now the larger sub-cost within the token loop.

### 6. Aggregate merge is now the second-largest non-prescan item

These sections are now measurably relevant:

- `aggregate_merge = 6640 ms` (7.3% of batch — up from 3.9% as other costs fell)
- `csv_lookup_loaded = 14560 ms` (16.1% — the inner hash probe cost shared across prescan and token-loop paths)
- `tlv_add_entry = 6040 ms` (6.6%)
- `tokenize = 4480 ms` (4.9%)
- `record_init = 1040 ms` (1.1%)

`aggregate_merge` and `csv_lookup_loaded` are the most useful next investigation targets after further prescan work.

### 7. Failed attempts should stay documented, but not define the baseline

The current benchmark does not support keeping the first prescan memoization change on performance grounds, so it has been reverted.

The most likely explanation is simple:

- the local memo table performs its own linear scan work on every profiled prescan lookup attempt
- the benchmark does not show enough repeated joined-token reuse for that extra work to pay for itself
- on 68000 and 68030 class CPUs, the added per-lookup bookkeeping is a poor trade unless it removes a much larger amount of work

## Optimization Priority Order

Items 2 and 3 from the previous priority list (pack field CSV matching and general CSV lookup) have been largely resolved by the refactoring. The updated order is:

1. **Prescan lookup remains the dominant cost.** `prescan_lookup` at 35140 ms (38.9% of batch) is the single largest remaining hotspot. Further lookup reduction — such as first-token discriminators, suffix-only windows from corpus evidence, or span-walker redesign — is the highest priority for Amiga performance.
2. **`csv_lookup_loaded` at 14560 ms (16.1%)** is the underlying hash probe cost shared between prescan and token-loop paths. Improvements to probe density or cache layout benefit both paths simultaneously.
3. **`aggregate_merge` at 6640 ms (7.3%)** is now the second-largest non-prescan item and warrants profiling attention before the other sub-percent items.
4. **`token_checks` at 8260 ms (9.1%)** — the token evaluation logic is now more prominent. Investigate whether early-exit conditions can reduce per-token branch cost.
5. Revisit `tlv_add_entry` (6040 ms, 6.6%) and `record_init` (1040 ms, 1.1%) only after the higher-cost paths above are addressed.

The 68000 comparison benchmark supports the same ordering. It also adds one important caution:

- use profiled runs to rank hotspots
- use non-profiled runs to judge real user-facing throughput

---

## 68040 @ 40MHz — Original Code vs Optimised 68030 @ 40MHz

`benchmark-summary-68040_at_40mhz.txt` records the original unoptimised pipeline run on a
68040 @ 40MHz with `PROFILE=1`. This predates the refactoring work and all hotspot fixes.

The latest optimised result is Run 3 from `benchmark-summary.txt` (Issues 1+2+4 applied,
`PROFILE=1`, 68030 @ 40MHz). Run 4 (`PROFILE=0`, same 68030) gives the true wall-clock time.

Note: the 68040 pipeline is wider than the 68030 and slightly faster per clock for integer
work, so this comparison modestly understates the algorithmic improvement.

### Original 68040 profiled run (pre-refactoring)

```text
TLV build time: 196960 ms
TLV save time:  1700 ms

batch_total        calls=1      total=190600  ms share=100.0%
prescan            calls=3861   total=125020  ms share= 65.5%
prescan_join       calls=48086  total=12900   ms share=  6.7%
prescan_lookup     calls=48086  total=70760   ms share= 37.1%
prescan_rebuild    calls=137    total=100     ms share=  0.0%
token_loop         calls=3861   total=47520   ms share= 24.9%
pack_field_match   calls=918    total=31920   ms share= 16.7%
csv_lookup         calls=60758  total=62960   ms share= 33.0%
tlv_add_entry      calls=23268  total=5920    ms share=  3.1%
aggregate_merge    calls=1      total=6360    ms share=  3.3%
```

### Comparison table — profiled runs (same instrumentation overhead)

| Metric | Original 68040 (PROFILE=1) | Optimised 68030 Run 3 (PROFILE=1) | Delta |
|---|---|---|---|
| TLV build time | 196960 ms | **87580 ms** | **−109380 ms (−55.5%)** |
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

### True wall-clock (PROFILE=0, no instrumentation overhead)

| | |
|---|---|
| Optimised 68030 @ 40MHz, Run 4 | **20220 ms** |
| Optimised 68000 @ 7MHz, Run 5 | **97840 ms** |

The 20220 ms is not directly comparable to the 190600 ms (different profiling state), but
illustrates the combined effect. A PROFILE=0 run of the original 68040 code would likely
be in the 40–60 second range based on the ~3–4× profiling overhead ratio seen in other runs.

### What drove the improvement

| Optimisation | Primary effect |
|---|---|
| Shared `parts[]` / prescan restructure | Eliminated `prescan_join` (12900 ms) |
| Hash table for CSV lookups | Eliminated slow-path `csv_lookup` (62960 ms) |
| `pack_field_match` prehash + field sort + min/max gate | −91.7% on `pack_field_match` |
| Issue 2: pre-compute `is_debug_filename` | Eliminated ~87600 inner-loop `strstr` calls |
| Issue 1: part-length pre-screen | −20.4% fewer lookup calls |
| Issue 4: window loop range tightening | −4.6% on `prescan_lookup` time |

## Current Working Interpretation

The current profile suggests that the code is spending most of its time repeatedly asking questions like:

- "Does this joined prescan token exist in this CSV?"
- "Does this remaining token match any pack field CSV?"

The profile does not suggest that filename rebuilding, tokenization, or output writing are the main problems.

It also suggests that not all "avoid repeated lookup" strategies are automatically beneficial. On 68k hardware in particular, any optimization that adds another small-array scan, more branches, or more string comparisons inside the hottest loop should be treated skeptically unless it removes a clearly larger cost.

## Practical Next Step

The next optimization pass should remain on the prescan lookup path, keeping the same principle that proved out in the refactoring: prefer reducing work rather than adding extra structures in the hottest loop.

Specifically:

- prefer changes that reduce work directly rather than adding a second lookup structure in the hottest loop
- prefer a cheaper fast path, such as first-token discriminators or suffix-only windows that skip most candidate spans without probing the hash table
- if a length-gate or window-prune idea is reconsidered, prove first that the candidate reduction is large enough to outweigh the added branch cost on a simple in-order 68030 pipeline
- `pack_field_match` is now a minor cost (2.8%); do not prioritize it further until prescan_lookup and csv_lookup_loaded are reduced

## Benchmark Discipline

When comparing future optimization results, always compare against the post-refactoring default baseline (97420 ms, 2026-05-06) unless another benchmark is explicitly declared as the new baseline.

A future replacement baseline should only be adopted if:

- it is clearly documented here
- the hardware or emulation configuration is stated explicitly
- the full profile summary is captured alongside total build and save time

When profiling on very slow systems such as a 68000 with no Fast RAM, expect profiling overhead to materially distort wall-clock time. Preserve both kinds of numbers when possible:

- profiled build for hotspot analysis
- non-profiled build for realistic elapsed time
