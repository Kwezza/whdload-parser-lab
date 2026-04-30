[Profile]
id=phase1_singleton_exclude
name=Phase 1 Singleton Exclude Policy
version=1
profile_format=1

[Filter.language]
include=En
exclude=

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.video]
include=PAL,NTSC
exclude=NTSC

[Filter.memory]
include=1MbChip,1MBChip,1Mb,1MB,512KB,512k,LowMem
exclude=

[Filter.media]
include=Disk,HD,CD
exclude=

[Filter.special]
include=
exclude=Censored

[Scoring]
weight.language=100
weight.chipset=80
weight.video=30
weight.memory=20
weight.media=10
weight.special=0
