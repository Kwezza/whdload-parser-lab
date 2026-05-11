# T006: Excluded-only group produces no output.
# CD32OnlyGame has exactly one variant and it is excluded.
# The group must appear in groups_rejected, not selected_groups.
[Profile]
id=t006_cd32_excluded
name=T006 CD32 Excluded Group
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=CD32

[Scoring]
weight.chipset=100
