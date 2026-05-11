# T023: Missing lane is skipped without error.
# BucketMissing only has an AGA variant; the OCS lane finds no eligible
# variant and is skipped.
# Expected: BucketMissing_v1.0_AGA_En  Selection lanes: 2  Selected variants: 1
[Profile]
id=t023_bucket_missing
name=T023 Missing Lane Skipped
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS
exclude=

[Scoring]
weight.chipset=100
