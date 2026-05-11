# T025: Duplicate suppression across lanes.
# Lane 0 bucket {AGA} selects AGA.  Lane 1 bucket {AGA,OCS} would also
# prefer AGA, but AGA was already selected for this group so it must be
# skipped; OCS is chosen instead.
# Expected: BucketDup_v1.0_AGA_En + BucketDup_v1.0_OCS_En  (no duplicate AGA)
[Profile]
id=t025_dup_suppression
name=T025 Duplicate Suppression Across Lanes
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/AGA,OCS
exclude=

[Scoring]
weight.chipset=100
