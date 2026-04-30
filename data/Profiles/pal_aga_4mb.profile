# Built-in PAL AGA 4MB Profile
[Profile]
id=pal_aga_4mb
name=PAL AGA 4MB Default
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=PALNTSC

[Filter.language]
include=EN,DE
exclude=

[Filter.memory]
include=FAST8M,FAST4M,FAST2M,FAST1M
exclude=SLOW256K

[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100

