# Optimising a 68k Amiga filename pipeline

Your profile already points to the real answer: the biggest win is not “make `strcmp()` a bit faster,” but “stop doing so many lookups and rebuilds in the first place.” Given that `process_filename` is 98.1% of batch time, with `prescan`, `prescan_lookup`, `csv_lookup`, and `token_loop` dominating, the highest-return changes are the ones that delete candidate generation, delete full restarts, and delete hot-path name resolution. On classic 68k targets, that strategy is even more attractive because the hardware budget is small: the entity["company","Motorola","semiconductor company"] MC68030 only has 256-byte instruction and 256-byte data caches, both direct-mapped, and the MC68000’s `DIVU` is very expensive at 140 clocks plus effective-address time. citeturn6view2turn4view0turn5view2

## Ranked recommendations

The ranking below is by expected total speedup on your measured workload, not by implementation ease. The effects are not additive.

1. **Turn prescan into a pruned span walker instead of a per-field, per-window string builder.**  
   Highest expected gain. This directly attacks the 64.2% `prescan` bucket and the 35.7% `prescan_lookup` bucket by reducing how many candidate windows ever reach `csv_cache_lookup()`.

2. **Stop rebuilding and re-tokenising after every removable prescan match.**  
   Also potentially a very large gain. The current restart path multiplies work that was already expensive.

3. **Split `csv_cache_lookup()` into a hot loaded-cache path and a cold manager/lazy-load path, and pass pre-resolved cache handles or small ids.**  
   High expected gain. This removes repeated manager-name scans and repeated lazy-load branching from every lookup.

4. **Canonicalise case once and demote `csv_cache_find_ci()` to an instrumented cold path.**  
   High expected gain if the fallback fires at all; medium gain if it is rare. The important point is that the current fallback turns a miss into a full-table scan.

5. **Make hash misses cheaper: power-of-two capacities, bitmask wrap, short metadata before `strcmp()`, and a lower load factor for miss-heavy tables.**  
   Medium-to-high expected gain. Your prescan workload is exactly the kind of workload where miss cost matters more than hit cost.

6. **Flatten `pack_field_match`: pre-resolve field-to-cache bindings, split `&` once, and reorder field tests by likely hit probability.**  
   Medium expected gain. This should improve the 13.8% `pack_field_match` bucket and share the same lookup improvements as prescan.

7. **Special-case tiny fixed CSVs with sorted arrays or generated perfect hashes.**  
   Medium-to-low expected gain overall, but potentially excellent for a few hot, small vocabularies.

8. **Compiler tuning and tiny assembly helpers only after the work-reduction patches land.**  
   Lowest expected gain. Worth doing, but only after the algorithmic fixes. GCC’s documented optimization defaults and `gperf`’s documented design both reinforce the same point: structural reductions usually dominate small local codegen wins. citeturn15view0turn18view3

## Prescan path

**Recommendation: invert and prune candidate generation, and move to span-based lookup rather than rebuilding joined strings.**

The current order is effectively:

- for each field  
- for each window length  
- for each position  
- build a joined string  
- do a lookup

That order is expensive because the same candidate window gets rebuilt and re-looked-up across multiple fields. The better hot-path shape is:

- tokenize once
- for each start position
- for each allowed window length
- run cheap pruning first
- build nothing if pruning fails
- if the candidate survives, do one span-based lookup into only the relevant caches

The simplest safe pruning metadata to compute once per CSV at load time is: minimum token count, maximum token count, an allowed-token-count bitmask, and at least one cheap first-byte or first-token discriminator. A more effective version also stores token length and a small fingerprint per entry, so candidate windows with the wrong length or obviously wrong prefix never reach full string comparison. This is the same general principle that `gperf` exposes with `%compare-lengths`: reject impossible strings before calling the expensive comparator. On a 68030 with tiny direct-mapped caches, reducing buffer churn and helper-call traffic matters disproportionately. citeturn19view0turn6view2turn4view0

**Why it helps on 68k:**  
It deletes work before hashing, probing, and string comparison happen. That is exactly what you want on a CPU where cache footprint is small and repeated string work is expensive. It also attacks misses, which are usually more numerous than hits in exploratory prescan.

**Expected memory cost:**  
Low. A few bytes of metadata per cache, plus token offset arrays per filename. If you add per-entry length and a 16-bit fingerprint, that is still usually a modest memory trade.

**Implementation complexity:**  
Medium. The lowest-risk version keeps the existing CSV data structure and only changes the prescan loop ordering plus metadata gates. The more aggressive version introduces `lookup_span(cache, ptr, len, hash)` and stops materialising joined strings entirely.

**Risk to correctness:**  
Low to medium. Length/count pruning is very safe. First-token or position-based pruning is safe if derived directly from CSV contents. Tail-biased or suffix-only pruning has a higher correctness risk unless you prove it against the corpus.

**What benchmark counter should improve:**  
`prescan`, `prescan_lookup`, `csv_lookup`, and `token_loop`. Add new counters for `prescan_candidates_total`, `prescan_candidates_pruned_count`, `prescan_candidates_built`, and `prescan_span_lookups`.

**How to test it safely:**  
Keep the old prescan as a host-build reference. For every input filename in your corpus, compare exact extracted metadata, exact removed spans, and exact final stripped filename. Add a property-style test that enumerates token windows and verifies old joined-string lookup and new span lookup return identical ids.

A particularly useful sub-optimization here is to cap the outer window length globally. If the enabled prescan fields only contain entries up to, say, three tokens, then every four-token, five-token, and six-token candidate is impossible by construction and should never be built. That is a tiny patch with a potentially large effect.

A second useful sub-optimization is positional pruning. Your examples are suffix-shaped, with tags such as chipset and language toward the end of the archive name. If a corpus diff confirms that those prescan classes are suffix-only in practice, shift the scanner to the tail first, or introduce a strict suffix-only mode for those fields. That can collapse the search space dramatically, but it should be gated by corpus evidence because it is a semantic assumption rather than a mere data-structure improvement.

**Recommendation: eliminate full rebuild and full re-tokenisation after each removable match.**

The current “match, rebuild filename, re-tokenise from scratch, restart” design is algorithmically expensive because it repeats already-completed work after every removable hit. A better design is to tokenize once into an array of spans, mark matched token windows as dead or consumed, continue scanning the surviving token sequence, and only compact or rebuild the stripped filename once per filename, or once per prescan pass if you need multiple passes.

**Why it helps on 68k:**  
Re-copying the filename, re-splitting delimiters, and recreating token pointers is pure overhead. On a simple CPU, deleting those passes is worth more than shaving a few instructions from the tokenizer itself.

**Expected memory cost:**  
Low. One `alive` byte or a bitset entry per token, plus a compacted index array if desired.

**Implementation complexity:**  
Medium. The easiest version keeps the token array stable and marks consumed ranges. A more advanced version compacts the token array in place after each hit without reparsing text.

**Risk to correctness:**  
Medium. You must preserve current semantics when a removal exposes a new multi-token candidate that crosses the removed region. That is solvable, but it needs exact differential testing.

**What benchmark counter should improve:**  
`prescan`, `token_loop`, and `process_filename`. Add `prescan_restarts`, `filename_rebuilds`, and `retokenize_count`.

**How to test it safely:**  
Run both implementations side by side on the full corpus and compare full TLV output, not just individual fields. Also add targeted regression cases where removing one token makes a new multi-token removable phrase visible across the gap.

## Lookup flattening and case canonicalisation

**Recommendation: split `csv_cache_lookup()` into hot and cold APIs, and pass `CSVCache *` or numeric ids in hot loops.**

Today every hot lookup pays for:

- manager-level linear search across `manager->caches[]`
- repeated `strcmp()` against `csv_name`
- lazy-load branching
- then the actual table lookup

That is the wrong shape for a hotspot. The better separation is:

- cold path: resolve and load a cache once
- hot path: `csv_cache_lookup_loaded(cache, span_or_token)` with no manager search and no lazy-load logic

You should pre-resolve prescan fields and pack fields at initialization into either direct `CSVCache *` pointers or tiny numeric ids that index a compact pointer table. That also removes repeated `get_csv_filename_for_field()` work from `pack_field_match`.

**Why it helps on 68k:**  
It removes repeated string comparisons and repeated branchy cold-path logic from the innermost loop. Even if manager-level lookup is only a modest fraction per call, your call count is huge enough that the saved fixed overhead can still be meaningful.

**Expected memory cost:**  
Negligible. A pointer or small id per field.

**Implementation complexity:**  
Low. This is one of the best isolated patches because it leaves semantics unchanged.

**Risk to correctness:**  
Low, provided the cold resolver remains the source of truth.

**What benchmark counter should improve:**  
Primarily `csv_lookup`. Add counters for `csv_manager_lookup_calls`, `csv_manager_lookup_time`, `csv_lazy_load_checks`, and `csv_hot_lookup_calls`.

**How to test it safely:**  
Keep the old API as a wrapper around the new hot/cold split initially. Verify that both code paths produce identical ids for the same input token on host and Amiga builds. Then benchmark preloaded-only versus legacy mixed mode to measure how much of `csv_lookup` was manager overhead.

My expectation is that manager-name lookup is worth removing, but I would still measure it explicitly rather than assume it is the entire problem.

**Recommendation: canonicalise once and move `csv_cache_find_ci()` out of the hot path.**

This is the most obviously suspicious part of the current lookup stack. Exact lookup is fast-ish; the fallback is not. If the exact path misses and the code then scans the entire table with case-insensitive comparison, you have turned a hash miss into a linear full-table walk.

The strongest design here is:

- lowercase every CSV token once at load time, preferably with ASCII-only canonicalisation
- lowercase or case-fold filename tokens once during parsing
- look up only canonical lowercase tokens in the hot path
- keep `csv_cache_find_ci()` only as an instrumented compatibility path, a debug assertion path, or a one-time migration aid

If you need the original spelling for diagnostics, store it separately; do not keep the hot path dependent on it.

**Why it helps on 68k:**  
It turns repeated case folding from “every comparison” into “once per token.” It also removes the worst fallback shape in the entire hotspot chain.

**Expected memory cost:**  
Zero to low if you fold in place. Low to medium if you preserve original spellings alongside canonical forms.

**Implementation complexity:**  
Low to medium. The safe starting point is “canonical exact lookup plus counted fallback.” If the fallback count stays at zero on the corpus, remove it from release builds.

**Risk to correctness:**  
Low for ASCII-only tokens. Medium if the token universe may contain non-ASCII or locale-sensitive characters and you currently rely on locale-aware semantics.

**What benchmark counter should improve:**  
`csv_lookup`, and indirectly `prescan_lookup` and `pack_field_match`. Add `csv_find_ci_calls`, `csv_find_ci_hits`, and `csv_find_ci_hit_after_exact_miss`.

**How to test it safely:**  
Phase it in. First canonicalise but keep the fallback, with counters and sampled logging of fallback hits. If corpus and real runs show zero or negligible hits, compile the fallback out of the release hot path.

## Hash table structure for 68k

Open addressing gets slower as the table fills, especially on misses. Under the standard open-addressing analysis, unsuccessful lookup cost is bounded by `1 / (1 - α)`, so a half-full table averages about 2 probes on misses, while a 90% full table averages about 10. Linear probing also suffers clustering as the table fills, though it retains the practical advantage of contiguous storage and fewer cache misses than pointer-heavy chaining. For your workload, that matters because prescan likely generates many unsuccessful probes. On the hardware side, the MC68000’s `DIVU` is very expensive, and the MC68030’s caches are tiny and direct-mapped, so avoidable modulo and avoidable `strcmp()` traffic are both worth attacking directly. citeturn13view1turn13view0turn13view2turn5view2turn6view2turn4view0

**Recommendation: keep linear probing, but make the miss path much cheaper.**

The changes I would make together are:

- switch capacities to powers of two
- replace `% cache->capacity` with `& (capacity - 1)`
- store `len`
- store `first_char` or, better, a short fingerprint such as a 16-bit hash fragment
- compare those metadata cheaply before `strcmp()`
- target a lower load factor for the prescan-facing tables, especially if misses dominate

Storing `len` is especially attractive because it prevents hopeless `strcmp()` calls. That is the same design intuition `gperf` documents with `%compare-lengths`: length-first rejection can materially cut string comparisons. citeturn19view0

**Why it helps on 68k:**  
Bitmasking removes division from the probe loop, which is one of the few classic micro-optimizations that is unambiguously worth it on this target. Length and fingerprint checks turn many misses into a handful of integer compares instead of multiple bytewise string compares. Lowering the load factor is justified here because your workload is miss-heavy; miss cost rises quickly with load factor. citeturn5view2turn13view1

**Expected memory cost:**  
Low to medium. `len` plus a 16-bit fingerprint adds a few bytes per entry. Lowering load factor is the larger RAM trade: it increases slot count.

**Implementation complexity:**  
Medium. The entry-structure change is straightforward, but you will want a one-time loader migration and a microbenchmark harness.

**Risk to correctness:**  
Low. Fingerprints only guard `strcmp()`, not replace it, so they cannot introduce false positives if full compare still runs on metadata matches.

**What benchmark counter should improve:**  
`csv_lookup`, `prescan_lookup`, and possibly `pack_field_match`. Add `hash_probe_steps_hit`, `hash_probe_steps_miss`, `strcmp_calls`, `metadata_rejects`, and `max_probe_run`.

**How to test it safely:**  
Add a microbenchmark that runs known-hit and known-miss lookups against representative CSVs. Benchmark at several load factors. The key question is not just average probe length, but average string comparisons per miss.

I would **not** switch to double hashing first. The literature is right that double hashing reduces the clustering problem that linear probing exhibits, but it also adds another hash function and arithmetic in the probe loop. On your CPU family, I would first keep linear probing, lower the load factor, remove modulo, and reduce `strcmp()` frequency. Only if instrumentation still shows long pathological runs would I revisit probe policy. citeturn13view2turn13view0turn5view2

**Recommendation: use selective tiny-table alternatives for tiny, fixed vocabularies.**

Not every CSV deserves a hash table. The `gperf` documentation is directly relevant here: sorted arrays are space-efficient but take `O(log n)` binary search, while perfect hashes can recognize a static keyword set with at most one probe, and `gperf` can also emit `switch`-based code that may reduce both time and space for some inputs. That makes perfect sense for tiny, fixed vocabularies such as chipsets, small language/tag sets, or other CSVs that are stable and build-time known. It makes far less sense for large or user-swappable CSVs. citeturn18view3turn18view1

**Why it helps on 68k:**  
For tiny sets, hash metadata and probing overhead can dominate the actual compare. A compact sorted array or generated perfect hash can reduce RAM and simplify the lookup path.

**Expected memory cost:**  
Usually low, sometimes lower than the present hash shape.

**Implementation complexity:**  
Medium to high. Sorted-array lookup is easy. A generated perfect-hash pipeline is more work, especially if you want to preserve CSVs as the source of truth.

**Risk to correctness:**  
Low if generated from the CSV source during the build and validated automatically.

**What benchmark counter should improve:**  
`csv_lookup` for the specific targeted caches, not necessarily the whole batch.

**How to test it safely:**  
Start with one or two very small, very hot CSVs. Keep the generic path in place for everything else. Use build-time generation and compare generated ids against the CSV loader on the host build.

## Pack-field matcher

**Recommendation: pre-resolve field matchers, split `&` once, and share the same hot lookup API as prescan.**

Your secondary hotspot is doing exactly the kind of repeated work that small CPUs dislike:

- iterate fields for each token
- map field name to CSV name every time
- split `&` on the fly every time
- call a wrapper that re-enters the slow manager path

The fix is mostly plumbing:

- build a compact `FieldMatcher[]` once, with direct `CSVCache *` or ids
- sort that array by expected hit rate, or at least group cheap/common fields first
- split `&` tokens once per input token into subspans, not once per field
- reuse the same `lookup_loaded_span()` helper that prescan uses

If some fields only accept single-token tags, or only alphabetic tags, encode that in the matcher and skip them cheaply.

**Why it helps on 68k:**  
It reduces repeated name resolution, repeated branchy splitting logic, and repeated wrapper overhead. It also improves instruction locality by making the hot loop shorter and more uniform.

**Expected memory cost:**  
Negligible. A compact matcher array and a few temporary subspan records.

**Implementation complexity:**  
Low to medium. This is another excellent isolated patch.

**Risk to correctness:**  
Low. You are mostly moving decisions from runtime repetition into precomputed setup.

**What benchmark counter should improve:**  
`pack_field_match`, and secondarily `csv_lookup`. Add `pack_field_iterations`, `pack_field_lookup_calls`, `ampersand_split_count`, and `ampersand_subtoken_lookups`.

**How to test it safely:**  
Unit-test per-token behavior for plain tokens and ampersand-composite tokens. Then run the full corpus diff to verify that field resolution and tie-breaking are unchanged.

## 68k-specific C and compiler notes

The main 68k lesson here is that **algorithmic simplification beats heroic helper tuning**. That said, a few implementation details are still worth doing once the bigger changes are in place.

First, carry lengths and spans everywhere you can. Once you know token length, stop calling `strlen()`. Once you have canonical lowercase spans, stop calling `strcasecmp()` in the hot path. If lookup accepts `(ptr,len)`, you can use length and metadata to reject quickly and only do full string compare when everything else agrees.

Second, prefer one reusable scratch area per filename over repeated stack-buffer copying inside inner loops. Large stack frames and repeated stack traffic are not free, and keeping hot state in compact arrays is usually friendlier to these CPUs than rebuilding transient buffers over and over.

Third, keep hot helpers small and obvious. For GCC, the documented behavior matters: `-O2` does **not** imply function inlining, while `-O3` does enable `-finline-functions`. The vbcc documentation is also relevant: it exposes tunable inlining controls such as `-inline-size` and `-inline-depth`, and its manual notes that register allocation uses function register-usage information to reduce save/restore traffic between calls. That means flattening `csv_cache_lookup_loaded()` into a small helper is worth benchmarking with both toolchains, but only after the API is simplified enough for the compiler to see a small hot function. citeturn15view0turn9search1turn16search0

**Recommendation: treat assembly as a last-mile option only.**

A tiny hand-written helper can be justified if, after all structural fixes, a single helper still dominates a microbenchmark. The best candidates would be a tiny ASCII lowercase routine or a length-and-first-byte compare helper. But I would not start there. Your own profile says the dominant problem is volume of work, not the last few cycles of one compare function.

**Why it helps on 68k:**  
Potentially a small gain in a narrow hotspot, but only after the higher-level waste is gone.

**Expected memory cost:**  
Negligible.

**Implementation complexity:**  
Medium, because it adds maintenance cost, ABI concerns, and cross-toolchain friction.

**Risk to correctness:**  
Medium. String and case-fold helpers are easy to get subtly wrong.

**What benchmark counter should improve:**  
Only whatever helper still remains hot after the structural changes.

**How to test it safely:**  
Keep the C version as a reference, and use exhaustive host tests on ASCII inputs before trying on-target speed tests.

## Patch order, benchmarks, and limitations

If you want the **smallest, safest isolated patches first**, I would do them in this order:

1. **Add counters before changing behavior.**  
   Specifically: manager lookup calls/time, lazy-load checks, `csv_find_ci()` calls/hits, average probe length on hits and misses, `strcmp()` count, joined-string builds, and retokenize/rebuild count.

2. **Introduce `csv_cache_lookup_loaded()` and pre-resolve cache handles for prescan and pack fields.**  
   Small patch, low risk, high observability.

3. **Canonicalise lowercase once and keep `csv_cache_find_ci()` only as a counted fallback.**  
   Small patch, likely strong return.

4. **Add `len` plus `first_char` or a short fingerprint to hash entries, and move capacities to powers of two.**  
   Still reasonably isolated, and easy to microbenchmark.

5. **Flatten `pack_field_match`: pre-resolve field matchers and split `&` once.**  
   Another clean isolated patch.

6. **Add prescan pruning metadata without changing prescan semantics yet.**  
   Start with max-token-count caps and allowed-length masks.

7. **Rework prescan to build each candidate once, or to use span-based lookup.**  
   Bigger change, but likely the largest single payoff.

8. **Remove full rebuild/re-tokenize and replace it with consumed-token state.**  
   Another larger change that depends on having good differential tests.

9. **Only then try suffix-only or tail-biased prescan modes and tiny-table specializations.**

The short version is: **measure first, then flatten hot lookup, then kill the case-insensitive fallback, then optimize the miss path, then refactor prescan.**

There are three material open questions that affect exact prioritization. The first is how often `csv_cache_find_ci()` actually fires; if it is nearly zero, the fallback is a correctness cleanup more than a speed win. The second is the actual size distribution of your CSVs; that determines where sorted arrays or generated perfect hashes become attractive. The third is whether your real corpus supports suffix-only assumptions for some prescan fields. Until those are measured, exact speedup estimates should be treated as directional rather than guaranteed.

My highest-confidence conclusion is straightforward: **the first big speedup will come from reducing candidate count and restart count, and the next one will come from turning `csv_cache_lookup()` into a true hot-path function over already-loaded, already-canonical caches.** The rest matters, but those two changes are where classic 68k hardware is most likely to reward you.