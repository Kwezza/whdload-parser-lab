# T033: Cartesian product of slash buckets exceeds lane limit (32).
# chipset 8 buckets x language 5 buckets = 40 lanes > FP_MAX_SELECTION_LANES.
# Expected: harness returns non-zero, error mentions selection lane limit.
[Profile]
id=t033_too_many_lanes
name=T033 Too Many Selection Lanes
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/ECS/OCS/CD32/CDTV/AAA/RTG/PAL
exclude=

[Filter.language]
include=En/De/Fr/Es/It
exclude=

[Scoring]
weight.chipset=100
weight.language=100
