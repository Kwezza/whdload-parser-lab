# T032: Too many slash buckets in one field is fatal.
# 9 buckets exceed the per-field limit (FP_MAX_BUCKETS_FIELD=8).
# Expected: harness returns non-zero, error mentions bucket limit, no crash.
[Profile]
id=t032_too_many_buckets
name=T032 Too Many Slash Buckets
version=1
profile_format=1
debug=0

[Filter.chipset]
include=A/B/C/D/E/F/G/H/I
exclude=

[Scoring]
weight.chipset=100
