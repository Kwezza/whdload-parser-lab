# Host Quick-Screen Baseline - 2026-05-02

## Purpose

This document records the current host-side profiling baseline used for quick screening before Amiga validation.

It is not a replacement for Amiga timing.
It exists to answer a narrower question quickly: did a code change move hotspot distribution in the expected direction on the same host machine and build configuration?

## Host Quick-Screen Configuration

- Platform: Windows host build
- Build mode: `TARGET=host PROFILE=1`
- Input DAT: `assets_raw/Games(19-05-2025).dat`
- Output TLV: `output/Games(19-05-2025).tlv`
- CSV folder: `assets_raw/defs`
- Pack types: `assets_raw/prefs/pack_types.ini`
- Intended use: compare relative hotspot movement between nearby changes

Important notes:

- Host timings are now high-resolution enough to show sub-millisecond buckets.
- Absolute host timings are not comparable to classic Amiga timings.
- Use this file only as a same-machine, same-build quick-screen baseline.

## Baseline Summary

Captured from `build/host/benchmark-summary.txt` after the host high-resolution timing update.

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
TLV build time: 49 ms
TLV save time: 1 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=       0.202 ms avg=       0.202 ms max=       0.202 ms share=  0.4%
batch_total        calls=1      total=      46.080 ms avg=      46.080 ms max=      46.080 ms share=100.0%
record_init        calls=3861   total=       0.002 ms avg=       0.000 ms max=       0.001 ms share=  0.0%
process_filename   calls=3861   total=      44.049 ms avg=       0.011 ms max=       0.118 ms share= 95.5%
sanitize           calls=3861   total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
prescan            calls=3861   total=      11.289 ms avg=       0.002 ms max=       0.085 ms share= 24.4%
prescan_join       calls=48971  total=       0.013 ms avg=       0.000 ms max=       0.005 ms share=  0.0%
prescan_lookup     calls=48971  total=       0.037 ms avg=       0.000 ms max=       0.006 ms share=  0.0%
prescan_rebuild    calls=141    total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
tokenize           calls=3861   total=       0.073 ms avg=       0.000 ms max=       0.012 ms share=  0.1%
token_loop         calls=3861   total=      28.059 ms avg=       0.007 ms max=       0.087 ms share= 60.8%
token_checks       calls=7653   total=       0.073 ms avg=       0.000 ms max=       0.013 ms share=  0.1%
pack_field_match   calls=914    total=      27.491 ms avg=       0.030 ms max=       0.080 ms share= 59.6%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup         calls=3337   total=      25.834 ms avg=       0.007 ms max=       0.054 ms share= 56.0%
csv_lookup_loaded  calls=55066  total=       0.027 ms avg=       0.000 ms max=       0.005 ms share=  0.0%
tlv_add_entry      calls=23268  total=       0.232 ms avg=       0.000 ms max=       0.040 ms share=  0.5%
aggregate_merge    calls=1      total=       0.892 ms avg=       0.892 ms max=       0.892 ms share=  1.9%
```

## What To Compare First

For nearby optimization passes, compare these buckets first:

- `process_filename`
- `token_loop`
- `pack_field_match`
- `csv_lookup`
- `csv_lookup_loaded`
- `prescan`

## Expected Good Direction

- `csv_lookup` decreases materially
- `csv_lookup_loaded` may increase
- `pack_field_match` and `token_loop` decrease with it
- `process_filename` decreases overall

## First Comparison Run

Tested change:

- moved language token parsing from repeated `csv_cache_lookup("Language", ...)` calls to a pre-resolved loaded cache when cache mode is enabled

Result summary:

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
TLV build time: 49 ms
TLV save time: 1 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=       0.198 ms avg=       0.198 ms max=       0.198 ms share=  0.4%
batch_total        calls=1      total=      46.006 ms avg=      46.006 ms max=      46.006 ms share=100.0%
record_init        calls=3861   total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
process_filename   calls=3861   total=      43.960 ms avg=       0.011 ms max=       0.098 ms share= 95.5%
sanitize           calls=3861   total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
prescan            calls=3861   total=      11.118 ms avg=       0.002 ms max=       0.044 ms share= 24.1%
prescan_join       calls=48971  total=       0.002 ms avg=       0.000 ms max=       0.002 ms share=  0.0%
prescan_lookup     calls=48971  total=       0.057 ms avg=       0.000 ms max=       0.041 ms share=  0.1%
prescan_rebuild    calls=141    total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
tokenize           calls=3861   total=       0.085 ms avg=       0.000 ms max=       0.014 ms share=  0.1%
token_loop         calls=3861   total=      28.096 ms avg=       0.007 ms max=       0.086 ms share= 61.0%
token_checks       calls=7653   total=       0.059 ms avg=       0.000 ms max=       0.012 ms share=  0.1%
pack_field_match   calls=914    total=      27.548 ms avg=       0.030 ms max=       0.063 ms share= 59.8%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup         calls=1828   total=      25.931 ms avg=       0.014 ms max=       0.037 ms share= 56.3%
csv_lookup_loaded  calls=55066  total=       0.051 ms avg=       0.000 ms max=       0.041 ms share=  0.1%
tlv_add_entry      calls=23268  total=       0.256 ms avg=       0.000 ms max=       0.041 ms share=  0.5%
aggregate_merge    calls=1      total=       1.112 ms avg=       1.112 ms max=       1.112 ms share=  2.4%
```

### Comparison Against Baseline

- `batch_total`: `46.080 ms` -> `46.006 ms` (`-0.074 ms`, essentially flat)
- `process_filename`: `44.049 ms` -> `43.960 ms` (`-0.089 ms`, essentially flat)
- `csv_lookup` calls: `3337` -> `1828` (`-1509`, clear reduction)
- `csv_lookup` total: `25.834 ms` -> `25.931 ms` (`+0.097 ms`, flat to slightly worse)
- `csv_lookup_loaded` total: `0.027 ms` -> `0.051 ms` (`+0.024 ms`, as expected from shifting work)
- `token_loop`: `28.059 ms` -> `28.096 ms` (`+0.037 ms`, flat)
- `pack_field_match`: `27.491 ms` -> `27.548 ms` (`+0.057 ms`, flat)

### Interpretation

- The lookup path shift worked in structural terms because a large number of cold `csv_lookup` calls moved away from the wrapper path.
- The timing result is effectively neutral on host.
- This is not a strong enough host-side win to justify an Amiga validation run by itself.
- The next quick-screen experiment should target a path that can reduce `pack_field_match` and `token_loop` total time, not just lookup call classification.

## Second Comparison Run

Tested change:

- excluded fields with dedicated handling paths from the generic pack-field CSV matcher loop
- specifically removed `version`, `language`, `sps`, and `contributors` from the generic inner-loop candidate set

Result summary:

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
TLV build time: 36 ms
TLV save time: 1 ms

TLV pipeline profile (save excluded):
session_init       calls=1      total=       0.200 ms avg=       0.200 ms max=       0.200 ms share=  0.6%
batch_total        calls=1      total=      32.726 ms avg=      32.726 ms max=      32.726 ms share=100.0%
record_init        calls=3861   total=       0.009 ms avg=       0.000 ms max=       0.004 ms share=  0.0%
process_filename   calls=3861   total=      30.687 ms avg=       0.007 ms max=       0.093 ms share= 93.7%
sanitize           calls=3861   total=       0.004 ms avg=       0.000 ms max=       0.004 ms share=  0.0%
prescan            calls=3861   total=      10.952 ms avg=       0.002 ms max=       0.047 ms share= 33.4%
prescan_join       calls=48971  total=       0.018 ms avg=       0.000 ms max=       0.009 ms share=  0.0%
prescan_lookup     calls=48971  total=       0.070 ms avg=       0.000 ms max=       0.043 ms share=  0.2%
prescan_rebuild    calls=141    total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
tokenize           calls=3861   total=       0.063 ms avg=       0.000 ms max=       0.012 ms share=  0.1%
token_loop         calls=3861   total=      14.888 ms avg=       0.003 ms max=       0.064 ms share= 45.4%
token_checks       calls=7653   total=       0.054 ms avg=       0.000 ms max=       0.009 ms share=  0.1%
pack_field_match   calls=914    total=      14.346 ms avg=       0.015 ms max=       0.043 ms share= 43.8%
unknown_token      calls=21     total=       0.000 ms avg=       0.000 ms max=       0.000 ms share=  0.0%
csv_lookup         calls=914    total=      13.522 ms avg=       0.014 ms max=       0.040 ms share= 41.3%
csv_lookup_loaded  calls=54056  total=       0.065 ms avg=       0.000 ms max=       0.043 ms share=  0.1%
tlv_add_entry      calls=23268  total=       0.319 ms avg=       0.000 ms max=       0.055 ms share=  0.9%
aggregate_merge    calls=1      total=       1.128 ms avg=       1.128 ms max=       1.128 ms share=  3.4%
```

### Comparison Against Baseline

- `batch_total`: `46.080 ms` -> `32.726 ms` (`-13.354 ms`, 29.0% faster)
- `process_filename`: `44.049 ms` -> `30.687 ms` (`-13.362 ms`, 30.3% faster)
- `token_loop`: `28.059 ms` -> `14.888 ms` (`-13.171 ms`, 46.9% faster)
- `pack_field_match`: `27.491 ms` -> `14.346 ms` (`-13.145 ms`, 47.8% faster)
- `csv_lookup` calls: `3337` -> `914` (`-2423`, 72.6% fewer)
- `csv_lookup` total: `25.834 ms` -> `13.522 ms` (`-12.312 ms`, 47.7% faster)
- `csv_lookup_loaded` calls: `55066` -> `54056` (`-1010`, small reduction)
- `prescan`: `11.289 ms` -> `10.952 ms` (`-0.337 ms`, slight improvement)

### Interpretation

- This is the first clear host-side win after the new quick-screen baseline was established.
- The improvement is concentrated exactly where expected: generic pack-field matching, CSV lookup volume, and token-loop time.
- The change is large enough to justify Amiga validation next.
- The remaining dominant hot buckets are now `pack_field_match`, `csv_lookup`, and `prescan`, but the generic matcher loop is much less inflated than before.