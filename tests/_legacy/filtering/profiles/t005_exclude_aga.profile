# T005: Exclude beats high score.
# AGA has the highest include-list rank but is also excluded.
# Banshee_v1.0_OCS_En must win even though OCS ranks lower.
[Profile]
id=t005_exclude_aga
name=T005 Exclude Beats High Score
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=AGA

[Scoring]
weight.chipset=200
