# T008: Missing chipset uses CSV default (OCS).
# DefaultChipGame_v1.0_En has no chipset field; the harness must treat it
# as the CSV default token (OCS, id=2).  With include=OCS, that variant
# must score and win over DefaultChipGame_v1.0_AGA_En.
[Profile]
id=t008_ocs_preferred
name=T008 OCS Preferred / CSV Default Test
version=1
profile_format=1
debug=0

[Filter.chipset]
include=OCS
exclude=

[Scoring]
weight.chipset=100
