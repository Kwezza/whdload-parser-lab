# T024: Global exclude removes OCS before lane selection.
# Even though OCS is in a lane bucket, exclude=OCS must eliminate it
# globally so the OCS lane is always empty.
# Expected: BucketExclude_v1.0_AGA_En only  (OCS variant never appears)
[Profile]
id=t024_exclude_global
name=T024 Global Exclude Before Lanes
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS
exclude=OCS

[Scoring]
weight.chipset=100
