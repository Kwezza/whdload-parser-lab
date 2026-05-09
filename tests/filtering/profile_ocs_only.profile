# Fixture profile: OCS only
# Used by Stage J regression tests.
# Groups 2-4 (Banshee, CannonFodder, DynaBlaster) are all-AGA and will be
# rejected in full, exercising the rejected_groups_count counter.
[Profile]
id=fixture_ocs_only
name=Fixture OCS Only
version=1
profile_format=1
debug=0

[Filter.chipset]
include=OCS
exclude=AGA

[Filter.language]
include=En

[Scoring]
weight.chipset=10
weight.language=5
