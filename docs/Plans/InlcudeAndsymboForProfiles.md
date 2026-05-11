You are working in the variant_backport_staging project.

Context
=======

This project builds and filters Amiga WHDLoad TLV files. The filtering runtime currently loads a TLV, binds a .profile file, groups variants by group_id, scores variants, and selects the best archive per game group.

A new profile feature is required: allow profile include lists to request multiple selected variants from the same game group using "/" inside include values.

This must support "/" in more than one [Filter.<field>] section, but with fixed Amiga-safe limits.

Important: the TLV filtering system has not been released yet, so no public backwards compatibility migration is required. However, existing comma-only profiles must continue to behave naturally as single-winner profiles.

User-facing syntax
==================

Comma "," keeps its existing meaning:

    include=AGA,ECS,OCS

This means:
- one selection bucket
- select one best variant per game group
- prefer AGA over ECS over OCS

Slash "/" is the new selection bucket separator:

    include=AGA/ECS,OCS

This means:
- two selection buckets
- bucket 0: AGA
- bucket 1: ECS,OCS
- select up to two variants per game group
- the second bucket prefers ECS over OCS

Another example:

    include=AGA/ECS/OCS

This means:
- three selection buckets
- bucket 0: AGA
- bucket 1: ECS
- bucket 2: OCS
- select up to three variants per game group

Multiple fields may use "/":

    [Filter.chipset]
    include=AGA/ECS,OCS
    exclude=CD32,CDTV

    [Filter.special]
    include=EarlyBuild/HiRes,LoRes/RemonsteredEdition
    exclude=BETA

This should produce selection lanes from the Cartesian product of slash buckets:

    lane 0: chipset AGA     AND special EarlyBuild
    lane 1: chipset AGA     AND special HiRes or LoRes
    lane 2: chipset AGA     AND special RemonsteredEdition
    lane 3: chipset ECS/OCS AND special EarlyBuild
    lane 4: chipset ECS/OCS AND special HiRes or LoRes
    lane 5: chipset ECS/OCS AND special RemonsteredEdition

For each game group, the selector should try to output the best variant matching each lane.

Terminology
===========

Use these terms in code comments and docs if they fit the existing style:

- include token: one resolved token ID from an include list
- bucket: one slash-separated include group
- selection lane: one generated combination of buckets across slash-enabled fields

Examples:

    include=AGA,ECS,OCS

    bucket 0 = AGA,ECS,OCS
    lane count = 1

    include=AGA/ECS,OCS

    bucket 0 = AGA
    bucket 1 = ECS,OCS
    lane count = 2 if this is the only slash field

    chipset buckets: AGA / ECS,OCS
    special buckets: EarlyBuild / HiRes,LoRes / RemonsteredEdition

    lane count = 2 * 3 = 6

Required limits
===============

Use fixed compile-time caps. Do not make this unbounded.

Add limits similar to:

    #define FP_MAX_BUCKET_FIELDS   4
    #define FP_MAX_BUCKETS_FIELD   8
    #define FP_MAX_SELECTION_LANES 32

Use existing project naming/style if different.

Meaning:

- FP_MAX_BUCKET_FIELDS:
  Maximum number of filter fields in one profile that may use slash buckets.

- FP_MAX_BUCKETS_FIELD:
  Maximum number of slash buckets allowed inside a single include list.

- FP_MAX_SELECTION_LANES:
  Maximum number of generated selection lanes after combining all slash-enabled fields.

If the generated lane count exceeds FP_MAX_SELECTION_LANES, reject the profile or filter run with a clear error. Do not silently truncate lanes.

If a single field exceeds FP_MAX_BUCKETS_FIELD, reject the profile or set a hard profile error. Avoid silently changing what the user requested.

If more than FP_MAX_BUCKET_FIELDS fields use "/", reject the profile or set a hard profile error.

Prefer explicit failure over silent truncation for this feature. Curated profiles must be trustworthy.

Core semantics
==============

1. Existing comma-only profiles must still select one winner per group.

2. "/" activates multi-bucket selection.

3. Multiple slash-enabled fields combine as AND requirements per lane.

4. Comma remains priority order inside each bucket.

5. Exclude always wins:
   - exclude tokens are global for that field
   - if a variant matches any exclude rule, reject the entire variant
   - excluded variants must not be selected for any lane

6. Missing lanes are not fatal:
   - if no variant in a group matches a generated lane, skip that lane
   - do not reject the whole group just because one lane is missing

7. Avoid duplicate output:
   - the same archive filename must not be output twice for the same group
   - this matters for overlapping buckets such as include=AGA/AGA,ECS
   - if the best candidate for a later lane was already selected for that group, choose the next-best candidate for that lane if available
   - if no alternative exists, skip that lane

8. Do not change group_id:
   - group_id remains the logical game grouping key
   - do not split group_id by chipset, special tag, language, or any other field
   - this is a selection policy, not a TLV format change

9. Do not change the TLV file format.

10. Search remains group-level:
    - if --search lotus* is supplied, first match candidate groups
    - then run lane selection inside each matched group

Special.csv note
================

Special.csv may contain entries that overlap semantically with other fields, for example combined language/support tags such as:

    EnFrDe
    EnFrEs

Do not try to solve that metadata modelling issue in this task.

For now:
- treat Special.csv tokens as normal tokens in their own field
- do not try to merge them with Language.csv
- do not add special-case logic for EnFrDe, EnFrEs, or similar tags
- document only the generic slash-bucket system

Implementation guidance
=======================

A. Extend profile include parsing
---------------------------------

Find the existing profile parser, likely around:

    src_raw/profile_loader.c
    src_raw/filter_profile.c
    relevant headers

The parser currently treats include as a comma-separated priority list.

Extend it so it recognises both "," and "/":

- "," ends the current token inside the current bucket
- "/" ends the current token and starts a new bucket
- trim whitespace around tokens
- ignore leading/trailing empty buckets if sensible
- preserve existing empty include behaviour

Important empty include rule:

    include=

must preserve the existing meaning:
- accept all values for that field
- no include scoring from that field unless existing logic says otherwise
- it must not become "select nothing"

Suggested compact representation:

    include_ids[] remains a flat token array
    include_count remains total token count

Add bucket metadata, for example:

    typedef struct FP_IncludeBucket {
        uint8_t start;
        uint8_t count;
    } FP_IncludeBucket;

In each field profile:

    FP_IncludeBucket buckets[FP_MAX_BUCKETS_FIELD];
    uint8_t bucket_count;

For:

    include=AGA/ECS,OCS

store:

    include_ids = [AGA, ECS, OCS]
    include_count = 3

    bucket_count = 2
    bucket[0] = start 0, count 1
    bucket[1] = start 1, count 2

For comma-only:

    include=AGA,ECS,OCS

store:

    include_ids = [AGA, ECS, OCS]
    include_count = 3

    bucket_count = 1
    bucket[0] = start 0, count 3

If this exact representation clashes with the existing code, use a similar fixed-array design.

B. Build a selection plan
-------------------------

After profile binding, build a compact selection plan from all fields that have more than one bucket.

Suggested structure:

    typedef struct FP_LaneRequirement {
        uint8_t field_index;       /* bound profile field index */
        uint8_t bucket_index;      /* bucket within that field */
    } FP_LaneRequirement;

    typedef struct FP_SelectionLane {
        uint8_t requirement_count;
        FP_LaneRequirement requirements[FP_MAX_BUCKET_FIELDS];
    } FP_SelectionLane;

    typedef struct FP_SelectionPlan {
        uint8_t lane_count;
        uint8_t bucket_field_count;
        FP_SelectionLane lanes[FP_MAX_SELECTION_LANES];
    } FP_SelectionPlan;

The exact struct names can change to fit project style.

Rules:

- If no field uses "/", build one implicit lane with zero requirements.
- If one field uses "/", lane count equals that field's bucket count.
- If multiple fields use "/", lane count is the Cartesian product of bucket counts.
- If lane count exceeds FP_MAX_SELECTION_LANES, reject the profile/filter run with a clear error.
- If slash-enabled field count exceeds FP_MAX_BUCKET_FIELDS, reject with a clear error.
- If any field has more than FP_MAX_BUCKETS_FIELD buckets, reject with a clear error.

Do not allocate lanes dynamically. Keep the plan fixed-size.

C. Lane matching
----------------

Add a helper similar to:

    variant_matches_selection_lane(variant, bound_profile, lane)

It should:

- for each lane requirement:
  - find the relevant field
  - get the bucket's token IDs
  - check whether the variant has an effective token matching that bucket
- all lane requirements must match
- if any requirement does not match, the variant is not eligible for that lane

Use integer token IDs only. Do not compare strings in Stage 7.

Default-token handling:

If the existing scorer uses CSV default tokens when a variant lacks a field, lane matching should ideally use the same effective-token logic. For example, if OCS is the default chipset and a variant has no chipset tag, then it should be eligible for an OCS bucket if the existing scoring system would treat it as OCS.

If reusing that logic is awkward, factor out a helper so scoring and lane matching stay consistent.

D. Selection/scoring behaviour
------------------------------

Update Stage 7 selection.

Current behaviour:

    for each group:
        score each variant
        select one best variant

New behaviour:

    for each group:
        clear selected-archive tracking for this group

        for each selection lane:
            best = none

            for each variant in group:
                if variant was already selected for this group:
                    keep it eligible only if duplicate handling later can choose alternatives,
                    but never output the same archive twice

                if variant does not match lane:
                    continue

                score variant using normal scorer

                if rejected by excludes:
                    continue

                if score is better than current best:
                    best = variant

            if best exists and has not already been output for this group:
                output/select best
            else if best was duplicate:
                select next-best non-duplicate candidate if available

Important:

- Scoring should still use all active profile fields.
- Language, memory, media, video, special, variant_tags, etc. should keep contributing according to weights.
- Excludes must remain absolute.
- Existing tie-breaking should remain deterministic, preferably first-encountered TLV order.

E. Ranking inside buckets
-------------------------

For:

    include=AGA/ECS,OCS

Inside the second bucket, ECS should be preferred over OCS.

Preferred behaviour:

- bucket-local ranks:
  - bucket 0: AGA rank 0
  - bucket 1: ECS rank 0, OCS rank 1

This avoids ECS being penalised merely because AGA exists in another bucket.

If the existing scorer relies heavily on one rank_by_id table per field, adjust it carefully. Possible options:

1. Add bucket-local rank lookup for lane scoring.
2. Keep global scoring but add a small lane/bucket match bonus.
3. Rebuild an effective rank table per lane if cheap enough.

Preferred option is bucket-local rank for the slash field. Keep it simple and fixed-size.

The required user-visible outcome is:

    include=AGA/ECS,OCS

should select:
- one AGA where available
- one ECS-or-OCS where available
- ECS should beat OCS if all else is equal

F. Host tests
-------------

Add or update host-side tests first. Do not start with Amiga-only testing.

Create focused tests for:

1. Existing comma-only behaviour

    [Filter.chipset]
    include=AGA,ECS,OCS

Expected:
- one selected archive per group

2. Two chipset buckets

    [Filter.chipset]
    include=AGA/ECS,OCS
    exclude=CD32,CDTV

Expected:
- up to two selected archives per group
- one AGA
- one ECS/OCS
- CD32/CDTV never selected

3. Three chipset buckets

    include=AGA/ECS/OCS

Expected:
- up to three selected archives per group

4. Multiple slash fields

    [Filter.chipset]
    include=AGA/ECS,OCS

    [Filter.special]
    include=EarlyBuild/HiRes,LoRes/RemonsteredEdition

Expected:
- lane count = 6
- selected variants match AND requirements for each lane

5. Missing lanes

A group has only AGA.

    include=AGA/ECS,OCS

Expected:
- output AGA only
- no error

6. Exclude wins

    include=AGA/CD32
    exclude=CD32

Expected:
- CD32 not output

7. Search interaction

Use --search lotus* or equivalent.

Expected:
- groups are matched by search first
- multi-lane selection runs only inside matched groups

8. Duplicate protection

    include=AGA/AGA,ECS

Expected:
- same archive not written twice for same group

9. Lane cap exceeded

Create a profile that generates more than 32 lanes.

Expected:
- profile/filter run fails clearly
- no silent truncation

10. Too many slash fields

Create more than 4 fields with slash buckets.

Expected:
- profile/filter run fails clearly

11. Too many buckets in one field

Create more than 8 buckets in one field.

Expected:
- profile/filter run fails clearly

G. Harness output
-----------------

The result file can now contain more selected archive filenames than there are selected groups.

Update harness summary wording if needed.

Prefer wording such as:

    Selected variants : N
    Selected groups   : M
    Selection lanes   : L

If search is active:

    Matched groups    : M
    Selection lanes   : L
    Selected variants : N

Do not make reusable filtering modules print directly. Keep console output in the harness/caller.

H. Example profiles
-------------------

Update the example profiles that ship with WHDFetch.

At least one shipped profile should be heavily commented and explain:

- [Profile]
- [Filter.<field>]
- include
- exclude
- comma priority
- slash buckets
- multiple slash fields combine as AND lanes
- [Scoring]
- weight values
- weight 0 means no score contribution but excludes still apply

Example comment block:

    [Filter.chipset]
    # Comma means priority order inside one selection bucket:
    #   include=AGA,ECS,OCS
    # selects one version, preferring AGA, then ECS, then OCS.
    #
    # Slash creates another selection bucket:
    #   include=AGA/ECS,OCS
    # can select one AGA version and one ECS-or-OCS version
    # from the same game group.
    #
    # Multiple fields may use slash. Slash fields combine as AND lanes.
    # For example, chipset AGA/ECS and special EarlyBuild/HiRes
    # can select AGA+EarlyBuild, AGA+HiRes, ECS+EarlyBuild, ECS+HiRes.
    #
    # Exclude always wins. A CD32 variant is rejected even if it
    # would otherwise match an include bucket.
    include=AGA,ECS,OCS
    exclude=CD32,CDTV

Do not make every profile unreadably noisy. One heavily commented reference profile plus lighter comments elsewhere is fine.

I. Documentation
----------------

When code and host tests pass, update:

    docs\tlv-filtering-overview.md

Also update any profile-system documentation if present.

Document:

- comma-only include lists preserve single-winner behaviour
- "/" splits include lists into selection buckets
- each bucket can produce one selected variant per game group
- comma inside a bucket is priority order
- multiple slash-enabled fields combine into AND selection lanes
- lane count is capped by FP_MAX_SELECTION_LANES
- slash-enabled field count is capped by FP_MAX_BUCKET_FIELDS
- buckets per field are capped by FP_MAX_BUCKETS_FIELD
- cap violations are errors, not silent truncation
- excludes apply globally and always win
- missing lanes are skipped
- duplicate archive output is suppressed per group
- search remains group-level before selection
- group_id is unchanged
- TLV format is unchanged
- Special.csv overlap with Language.csv-style tags is a metadata issue outside this task

J. Constraints
--------------

- Do not change the TLV file format.
- Do not change group_id assignment or semantics.
- Do not split groups by chipset or special tag.
- Do not compare strings in the hot selection path.
- Keep reusable filter code independent from stdout/stderr.
- Keep test harness code separate from reusable filtering code.
- Prefer fixed-size arrays.
- Keep the implementation suitable for Amiga/vbcc/68k constraints.
- Avoid PowerShell in Makefiles or build scripts.
- Avoid a large abstract rules engine. This is still just profile include bucket selection.

K. Flexibility
--------------

You may adjust internal struct names, helper names, and exact file placement to match the existing codebase.

You may slightly adjust edge-case behaviour if host testing proves the exact proposed approach is awkward, but only if absolutely necessary.

If you adjust behaviour, document it in:

- relevant code comments
- tests
- docs\tlv-filtering-overview.md

Do not change the core user-facing syntax:

    include=AGA/ECS,OCS

That syntax must remain the feature.

L. Completion criteria
======================

The task is complete when:

1. Host build passes.
2. Existing comma-only profiles still select one winner per group.
3. Slash profiles can select multiple variants from the same group.
4. Multiple slash-enabled fields combine into AND selection lanes.
5. The fixed caps are implemented:
   - FP_MAX_BUCKET_FIELDS = 4
   - FP_MAX_BUCKETS_FIELD = 8
   - FP_MAX_SELECTION_LANES = 32
6. Cap violations produce clear errors.
7. Exclude rules still reject globally.
8. Search still filters groups before lane selection.
9. Duplicate archive output is suppressed per group.
10. Example profiles explain [Profile], [Filter], [Scoring], comma priority, slash buckets, multi-field lanes, and exclude behaviour.
11. docs\tlv-filtering-overview.md documents the new "/" system.
12. Any profile-system documentation is updated if present.
13. No TLV format change is introduced.