# T004: Combined scoring; chipset weight (200) > language weight (100).
# GameA_v1.0_AGA_De must beat GameA_v1.0_OCS_En because AGA chipset
# preference now carries more weight than English language preference.
[Profile]
id=t004_chipset_dominates
name=T004 Chipset Dominates
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=

[Filter.language]
include=En,De
exclude=

[Scoring]
weight.chipset=200
weight.language=100
