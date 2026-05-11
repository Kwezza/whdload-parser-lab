# Fixture profile: too many buckets in one field (9 > FP_MAX_BUCKETS_FIELD=8)
# T16: profile load must fail with a clear error; harness exits non-zero.
[Profile]
id=fixture_bucket_overflow
name=Fixture too-many buckets
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS/AGA/OCS/AGA/OCS/AGA/OCS/AGA

[Filter.language]
include=En
