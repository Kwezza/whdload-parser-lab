# T003: Combined scoring; language weight (200) > chipset weight (100).
# GameA_v1.0_OCS_En must beat GameA_v1.0_AGA_De because English language
# preference carries more weight than AGA chipset preference.
[Profile]
id=t003_lang_dominates
name=T003 Language Dominates
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
weight.chipset=100
weight.language=200
