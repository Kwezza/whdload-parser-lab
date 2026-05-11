# T012 / T013 / T014 / T015-T020 (shared):
# Simple AGA-over-OCS single-lane chipset profile.
# - T012: proves AlienBreed and AlienBreed2 are separate groups.
# - T013: proves FallbackGame groups correctly via derive_group_name.
# - T014: proves WeirdNameNoVersion names do not crash the grouper.
# - T015-T020: used as the scoring profile alongside --search options.
[Profile]
id=t012_t020_chipset_aga_ocs
name=T012-T020 Chipset AGA Over OCS
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=

[Scoring]
weight.chipset=100
