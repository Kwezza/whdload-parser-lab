# T021 / T022: One slash in chipset include -> two selection lanes.
# Lane 0: AGA (rank 0 within bucket {AGA})
# Lane 1: ECS preferred over OCS within bucket {ECS,OCS}
# Expected: BucketGame_v1.0_AGA_En + BucketGame_v1.0_ECS_En
#           Selection lanes: 2
[Profile]
id=t021_t022_bucket_chipset
name=T021/T022 Two-Bucket Chipset AGA / ECS,OCS
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/ECS,OCS
exclude=

[Scoring]
weight.chipset=100
