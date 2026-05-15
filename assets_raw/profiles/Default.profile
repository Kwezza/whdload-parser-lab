# Built-in PAL AGA 4MB Profile
[Profile]
id=pal_aga_4mb
name=Default for hi memory 16MB AGA Amiga
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Filter.language]
include=EN,DE
exclude=

[Filter.memory]
include=FAST64M,FAST32M,FAST16M,FAST15M,FAST12M,FAST8M,FAST4M,FAST2M,FAST1M,FAST512K,FAST_UNKNOWN,CHIP2M,CHIP1M,CHIP512K,CHIP_UNKNOWN,UNKNOWN2M,UNKNOWN1M,UNKNOWN512K
exclude=SLOW1M,SLOW512K,SLOW_UNKNOWN


[Filter.audio]
include=MT32,Audios,Standard,NoVoice,NoSpeech,NoMusic
exclude=

[Filter.variant_tags]
include=FASTSLAVE,CHIPSLAVE
exclude=LOWMEM,SLOWSLAVE

[Filter.gameplay_modifier]
include=Standard
exclude=Trainer

[Scoring]
weight.chipset=150
weight.language=120
weight.memory=100
weight.variant_tags=50
weight.audio=20
weight.gameplay_modifier=10
