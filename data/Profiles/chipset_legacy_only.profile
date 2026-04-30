# Simplified legacy (ECS/OCS) preference profile excluding AGA
[Profile]
id=chipset_legacy_only
name=Chipset Legacy Only
version=1
profile_format=1
debug=0

[Filter.chipset]
include=ECS,OCS
exclude=AGA

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
