# multi_bucket_reference.profile
#
# REFERENCE PROFILE — Selection Buckets (Slash Syntax)
#
# This profile is a heavily-commented reference showing every aspect of the
# slash "/" bucket separator introduced in the new filtering layer.  It is
# not intended to produce useful output on a real collection; use one of the
# other profiles in this directory for everyday filtering.
#
# ── Background ────────────────────────────────────────────────────────────
#
# By default a comma-separated include list selects ONE best variant per
# game group:
#
#     include=AGA,ECS,OCS
#
#     → one selection lane
#     → for each group the highest-scoring variant is selected
#       (AGA preferred over ECS over OCS)
#
# The slash "/" separator divides an include list into BUCKETS.  Each bucket
# produces an independent selection lane.  Up to one variant per group is
# selected per lane:
#
#     include=AGA/ECS,OCS
#
#     → bucket 0: AGA          (lane 0 selects the best AGA variant)
#     → bucket 1: ECS,OCS      (lane 1 selects the best ECS or OCS variant)
#     → up to 2 variants selected per group
#
# Within a bucket commas still encode priority (left = highest rank).
#
# ── Multi-field Cartesian Product ─────────────────────────────────────────
#
# When two or more fields use slash, the lanes are the Cartesian product of
# their bucket lists:
#
#     [Filter.chipset]
#     include=AGA/ECS,OCS      <- 2 chipset buckets
#
#     [Filter.language]
#     include=En/De            <- 2 language buckets
#
#     Lanes produced (2 × 2 = 4):
#       lane 0: chipset=AGA      AND language=En
#       lane 1: chipset=AGA      AND language=De
#       lane 2: chipset=ECS,OCS  AND language=En
#       lane 3: chipset=ECS,OCS  AND language=De
#
# For each group the selector tries to fill every lane independently.
#
# ── Scoring Inside a Lane ─────────────────────────────────────────────────
#
# Within a lane, bucket-local rank is used for fields that carry a lane
# requirement.  This means ECS in bucket 1 has rank 0 (not its global rank
# relative to AGA), so the ranking is always relative to the bucket rather
# than the whole include list.
#
# Fields that do not use slash (e.g. Memory below) use their full global
# rank as usual.
#
# ── Exclusion Interaction ─────────────────────────────────────────────────
#
# exclude= is applied before lane scoring.  If a variant is excluded by ANY
# field it is removed from every lane.  Exclusion always beats inclusion.
#
# Example: include=AGA/OCS  exclude=OCS
#   → OCS variants are excluded in the rejection pre-pass
#   → lane 1 (OCS bucket) never finds a candidate and produces no output
#   → only AGA variants are selected
#
# ── Duplicate Suppression ─────────────────────────────────────────────────
#
# A variant can only appear ONCE in a group's output regardless of how many
# lanes could theoretically select it.  If lane 0 selects BreakOut_v1.0_AGA,
# lane 1 will skip that variant and try the next-best candidate.
#
# ── Hard Limits ───────────────────────────────────────────────────────────
#
# These limits are compile-time constants in include_raw/filtering/profile_binder.h:
#
#   FP_MAX_BUCKET_FIELDS   4   max fields that may use slash in one profile
#   FP_MAX_BUCKETS_FIELD   8   max slash-separated buckets per include list
#   FP_MAX_SELECTION_LANES 32  max generated Cartesian-product lanes
#
# Exceeding any limit causes the profile to be rejected at load time with a
# clear error message.  Lanes are never silently truncated.
#
# ── Output File Format ────────────────────────────────────────────────────
#
# The output file contains one filename per line.  When multiple lanes are
# active there may be multiple lines per game group:
#
#   AlienBreed_v1.0_AGA_En     <- lane 0 winner (AGA + En)
#   AlienBreed_v1.0_OCS_En     <- lane 2 winner (ECS,OCS + En)
#   AlienBreed_v1.0_OCS_De     <- lane 3 winner (ECS,OCS + De)
#   Banshee_v1.0_AGA_En        <- lane 0
#   Banshee_v1.0_AGA_De        <- lane 1 (AGA + De)
#   ...
#
# ── Console Summary (multi-lane) ──────────────────────────────────────────
#
# When at least one field uses slash the summary gains an extra line:
#
#   Selected variants : 9
#   Selected groups   : 5
#   Selection lanes   : 4
#
# For single-lane (comma-only) profiles the "Selection lanes" line is omitted
# to keep backward-compatible output.
#
# ─────────────────────────────────────────────────────────────────────────

[Profile]
id=multi_bucket_reference
name=Multi-Bucket Reference Profile (annotated)
version=1
profile_format=1
debug=0

# ── Filter.chipset ────────────────────────────────────────────────────────
#
# Two buckets:
#   bucket 0 = AGA           (best AGA variant per group)
#   bucket 1 = ECS,OCS       (best ECS-or-OCS variant, ECS preferred)
#
# Slash generates 2 selection lanes from this field alone.
# The second bucket uses a comma, so ECS outranks OCS within that bucket.
#
# CD32 and CDTV are excluded globally; they will never be selected by any lane.
#
[Filter.chipset]
include=AGA/ECS,OCS
exclude=CD32,CDTV

# ── Filter.language ───────────────────────────────────────────────────────
#
# Two buckets:
#   bucket 0 = En            (English variant)
#   bucket 1 = De            (German variant)
#
# Combined with the 2 chipset buckets this produces 4 lanes total (2 × 2).
#
[Filter.language]
include=En/De
exclude=

# ── Filter.memory ─────────────────────────────────────────────────────────
#
# Single bucket (no slash) — all lanes use the same memory preference list.
# FAST8M is most preferred; SLOW256K is excluded entirely.
#
[Filter.memory]
include=FAST8M,FAST4M,FAST2M,FAST1M
exclude=SLOW256K

# ── Scoring ───────────────────────────────────────────────────────────────
#
# Weights apply to every lane.  Chipset is the most important discriminator,
# followed by language, then memory.
#
[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
