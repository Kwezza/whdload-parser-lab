# Simplified AGA-first chipset profile (no language/memory weighting tweaks)
[Profile]
id=chipset_aga_only
name=Chipset AGA Only
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.language]
include=EN
exclude=

[Filter.memory]
include=
exclude=

[Scoring]
weight.chipset=200
weight.language=0
weight.memory=0
