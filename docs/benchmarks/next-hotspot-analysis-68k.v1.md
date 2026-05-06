# Next Hotspot Analysis For 68k Optimization

Date: 2026-05-01
Repository: GUI-WHDload-downloader
Branch: DevTLV
Working folder: variant_backport_staging

## Purpose

This note isolates the next code area that should be investigated based on the current 68030 @ 40MHz benchmark and the current source under variant_backport_staging.

The goal is not to propose one specific fix yet. The goal is to map the benchmark hotspot to the exact implementation methods now in use, so deeper 68k-focused optimization research can be aimed at the real code rather than the profile summary alone.

## Benchmark-Driven Priority

From the active default benchmark in docs/benchmarks/default-benchmark-68030-40mhz.md:

- process_filename = 157400 ms, 98.1% of batch_total
- prescan = 103040 ms, 64.2% of batch_total
- prescan_lookup = 57380 ms, 35.7% of batch_total
- csv_lookup = 52480 ms, 32.7% of batch_total
- token_loop = 38300 ms, 23.8% of batch_total
- pack_field_match = 22220 ms, 13.8% of batch_total

That means the next code area to inspect is not a broad "filename processing" problem. It is a specific chain:

1. prescan_and_strip_tokens()
2. csv_cache_lookup()
3. csv_cache_find() and csv_cache_find_ci()

After that, the next adjacent target is the pack_field_match path in the token loop, because it reuses the same CSV lookup machinery and is the next largest isolated bucket.

## Recommended Next Inspection Target

### Primary target

Prescan lookup path:

- src_raw/filename_processor.c:382 - prescan_and_strip_tokens()
- src_raw/csv_cache.c:714 - csv_cache_lookup()
- src_raw/csv_cache.c:203 - csv_cache_find()
- src_raw/csv_cache.c:228 - csv_cache_find_ci()

This is the best next target because:

- the benchmark says prescan_lookup is the single largest measured internal cost
- prescan calls csv_cache_lookup() in a high-frequency sliding-window loop
- the reverted memo experiment already showed that adding extra bookkeeping inside that loop is the wrong direction on 68k hardware

### Secondary target

Pack field matching path:

- src_raw/filename_processor.c:864 - pack_field_match loop
- src_raw/filename_processor.c:558 - csv_token_matcher_lookup()

This is second, not first, because it is smaller than prescan lookup but still large enough to matter after the prescan path is improved.

## Current Methods Used In The Slow Code

The important part for 68k research is not only where time is spent, but how the current code is doing the work.

### 1. Prescan uses repeated token-window enumeration

Current method in prescan_and_strip_tokens():

- copy the current processed filename into a temporary stack buffer
- split it into token pointers with whd_strtok_r()
- for each enabled prescan field, scan window sizes from longest down to 1
- for each window position, build a joined token string into a stack buffer
- call csv_cache_lookup() for every candidate joined token
- if a removable match is found, rebuild the filename and re-tokenize it from scratch

This is a simple and understandable algorithm, but it multiplies work in several dimensions:

- fields
- token count
- window count
- window positions
- CSV lookups per candidate
- occasional rebuild plus full re-tokenization

On a 68000/68030 class CPU, nested small-loop work is only acceptable if each inner iteration stays very cheap. Right now the inner iteration still includes string work and a multi-stage lookup path.

### 2. CSV lookup does two layers of search

Current method in csv_cache_lookup():

- linear search through manager->caches[] to find the CSV cache by csv_name
- if not found, lazy load the CSV
- hash lookup in csv_cache_find()
- if the exact lookup misses, full-table case-insensitive scan in csv_cache_find_ci()

This means one high-level prescan lookup is not a single constant-cost operation. It is currently a stack of smaller operations:

- compare CSV names in a loop
- perform hash computation
- probe the cache table with strcmp()
- possibly scan the entire cache linearly again with whd_strcasecmp()

That structure matters on 68k hardware because it adds more branches, more pointer chasing, and more repeated string comparisons than the profile summary alone suggests.

### 3. Pack field matching repeats the same lookup machinery

Current method in the token loop:

- for each remaining token, iterate every pack field in pack_info->field_list
- resolve csv_name for the field
- if the token contains '&', split it into parts on the fly
- call csv_token_matcher_lookup() for each candidate token or sub-token
- csv_token_matcher_lookup() is just a wrapper around csv_cache_lookup()

So the secondary hotspot is not really a different kind of work. It is another caller that drives the same CSV lookup stack repeatedly.

## Why This Matters For 68k-Class CPUs

The code is not slow because it is doing one obviously expensive thing. It is slow because the hottest path combines many medium-cost operations that are unfavorable on simple in-order processors.

The current code leans on methods that are usually reasonable on modern CPUs but need more scrutiny on 68k hardware:

- repeated nested loops over small arrays
- repeated string length checks and string comparisons
- stack-buffer assembly of temporary strings
- repeated branchy control flow inside the hottest loop
- manager-level lookup before the actual token lookup
- exact-match lookup followed by a second broader fallback search

For deep research, the main question should be:

"Which parts of this path can be removed or flattened outright, rather than wrapped in another helper or cache layer?"

That question fits 68k better than "what extra structure can be added around the hot path?"

## Concrete Code Areas To Research Next

### A. prescan_and_strip_tokens() inner lookup loop

What to research here:

- whether the sliding-window enumeration itself can avoid generating some candidates
- whether field-level setup can be hoisted so the inner loop has less work per candidate
- whether removable-match handling can avoid full re-tokenization after every strip
- whether the loop order is optimal for early exits on 68k

Why this is next:

- it directly owns prescan_lookup and prescan_join
- it decides how many times csv_cache_lookup() is called
- any reduction here cuts both prescan-specific and shared CSV lookup cost

### B. csv_cache_lookup() manager-level overhead

What to research here:

- whether csv_name resolution can be converted from repeated string search to a direct pointer or index resolved once per field
- whether case-insensitive fallback should be avoided for already normalized prescan tokens
- whether cache selection can be made branch-light for repeated same-CSV lookups

Why this is next:

- every prescan lookup passes through this layer
- every pack field CSV lookup also passes through this layer
- reducing this cost helps multiple benchmark buckets at once

### C. csv_cache_find_ci() fallback cost

What to research here:

- how often this fallback is actually needed in practice
- whether token normalization earlier in the pipeline can remove the need for the fallback on hot paths
- whether the fallback can be gated more cheaply than a full cache scan

Why this matters:

- it is the worst kind of 68k work in a hot path: a full linear scan with repeated case-insensitive string comparison
- even if it is not dominant on every call, its miss-path cost is structurally expensive

### D. pack_field_match nested loops

What to research here:

- whether pack fields can be pre-resolved once per pack into direct CSV cache handles or ids
- whether '&' splitting can be done once per token rather than inside every field attempt
- whether a field ordering change can improve early exits for common tokens

Why this is secondary:

- it is clearly important, but smaller than prescan lookup
- much of its cost may drop automatically if csv_cache_lookup() becomes cheaper

## Code Snippets Currently Taking The Most Time

These snippets are not the entire performance problem by themselves. They are the exact code shapes that dominate the current profile and should anchor any optimization research.

### 1. Prescan sliding-window lookup loop

File: src_raw/filename_processor.c
Function: prescan_and_strip_tokens()

```c
for (uint32_t window = max_window; window >= 1; window--) {
    if (pc < window) { continue; }
    for (uint32_t i = 0; i + window <= pc; i++) {
        char joined[MAX_TOKEN_LENGTH];
        TLV_PROFILE_START(join_profile_stamp);
        if (!build_joined_token(joined, sizeof(joined), parts, i, window)) {
            joined[0] = '\0';
        }
        TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_JOIN_CONSTRUCTION, join_profile_stamp);
        if (joined[0] == '\0') { continue; }

        TLV_PROFILE_START(lookup_profile_stamp);
        uint32_t id = csv_cache_lookup(csv_manager, cfg->csv_base, joined);
        TLV_PROFILE_END(TLV_PROFILE_SECTION_PRESCAN_CSV_LOOKUP, lookup_profile_stamp);
        if (id > 0) {
            if (cfg->field_id != 0) {
                tlv_record_add_entry(output_record, cfg->field_id,
                                     (const uint8_t *)&id, sizeof(id));
            }
            if (cfg->remove_from_filename) {
                ...
            }
        }
    }
}
```

Why this snippet matters:

- this loop is the direct caller behind prescan_lookup
- every extra instruction inside this loop is multiplied across 48086 prescan lookups in the default benchmark

### 2. CSV cache selection and lookup dispatch

File: src_raw/csv_cache.c
Function: csv_cache_lookup()

```c
CSVCache *target = NULL;
for (uint32_t i = 0; i < manager->cache_count; i++) {
    if (strcmp(manager->caches[i].csv_name, csv_name) == 0) {
        target = &manager->caches[i];
        break;
    }
}

if (!target) {
    if (!csv_cache_load_file(manager, csv_name)) {
        goto done;
    }
    if (manager->cache_count > 0) {
        target = &manager->caches[manager->cache_count - 1];
    }
}

if (target) {
    uint32_t id = csv_cache_find(target, token);
    if (id == 0) {
        id = csv_cache_find_ci(target, token);
    }
    result = id;
}
```

Why this snippet matters:

- this code is on the path of both prescan_lookup and pack_field_match
- it adds name-resolution overhead before the token lookup even starts
- it can also fall through to a second, much more expensive lookup path

### 3. Exact-match hash lookup with linear probing

File: src_raw/csv_cache.c
Function: csv_cache_find()

```c
uint32_t index = csv_hash_token(token, cache->capacity);
uint32_t original_index = index;

while (cache->entries[index].token != NULL) {
    if (strcmp(cache->entries[index].token, token) == 0) {
        return cache->entries[index].id;
    }

    index = (index + 1) % cache->capacity;
    if (index == original_index) {
        break;
    }
}

return 0;
```

Why this snippet matters:

- this is the exact-match core of every CSV token lookup
- any collision or repeated miss increases string-compare work
- modulo and repeated probing are both worth examining on 68k hardware

### 4. Case-insensitive full-cache fallback

File: src_raw/csv_cache.c
Function: csv_cache_find_ci()

```c
for (uint32_t i = 0; i < cache->capacity; i++) {
    if (cache->entries[i].token) {
        if (whd_strcasecmp(cache->entries[i].token, token) == 0) {
            return cache->entries[i].id;
        }
    }
}
return 0;
```

Why this snippet matters:

- this is the broadest miss-path scan in the lookup stack
- case-insensitive string comparison across the whole cache is exactly the kind of fallback that can be disproportionately expensive on 68k

### 5. Pack field matching nested lookup loop

File: src_raw/filename_processor.c
Function: process_filename() token loop

```c
for (uint32_t j = 0; j < pack_info->num_fields; j++) {
    const char *field_name = pack_info->field_list[j];
    const char *csv_name = get_csv_filename_for_field(field_registry, field_name);
    uint32_t token_id;

    if (!csv_name || csv_name[0] == '\0') {
        continue;
    }

    if (strchr(token, '&') != NULL) {
        ... split token into parts ...
        if (csv_token_matcher_lookup(part_buf, csv_name, field_registry,
                                     csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS) {
            ...
        }
    } else {
        if (csv_token_matcher_lookup(token, csv_name, field_registry,
                                     csv_manager, &token_id, &step_error) == PROCESSING_SUCCESS) {
            ...
        }
    }
}
```

Why this snippet matters:

- this is the next largest isolated caller of the CSV lookup machinery
- field iteration, optional token splitting, and lookup dispatch are all nested together

## Current Conclusion

The next code to look at is the prescan-to-CSV lookup chain, not a new cache wrapper around it.

The most likely high-value research direction is to reduce work in one of these two ways:

1. reduce how many times prescan asks the question
2. reduce the fixed cost of each csv_cache_lookup() call before and after the hash probe

For a 68k-class CPU, research should bias toward:

- fewer loops in the hot path
- fewer string comparisons in the hot path
- fewer fallback passes
- fewer temporary strings and repeated scans
- more direct field-to-cache resolution outside the innermost loops

That is the best fit to the current benchmark and to the hardware class.
