# Test variant of PAL AGA 4MB Profile adding French priority
[Profile]
id=pal_aga_4mb_test
name=PAL AGA 4MB Test With French
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=PALNTSC

[Filter.language]
include=FR,DE,EN
exclude=

[Filter.memory]
include=FAST8M,FAST4M,FAST2M,FAST1M
exclude=SLOW256K

[Scoring]
weight.chipset=150
weight.language=140
weight.memory=100
