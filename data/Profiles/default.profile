[Profile]
id=default
name=Default Variant Selection Profile
version=1
profile_format=1

[Filter.language]
include=Fr,En,De
exclude=

[Filter.chipset]
include=AGA,ECS,OCS
exclude=CD32,CDTV

[Filter.video]
include=PAL,NTSC
exclude=

[Filter.memory]
include=1MbChip,1MBChip,1Mb,1MB,512KB,512k,LowMem
exclude=

[Filter.media]
include=Disk,HD,CD
exclude=

[Filter.special]
include=
exclude=

[Scoring]
weight.language=100
weight.chipset=80
weight.video=30
weight.memory=20
weight.media=10
weight.special=0
