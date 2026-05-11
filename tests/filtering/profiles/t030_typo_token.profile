# T030: Unknown token typo does not crash.
# AGG is not a valid chipset token but must be stored as an unresolved hash
# rather than aborting the profile load.  AGA must still score and win.
# Expected: return code 0, TypoGame_v1.0_AGA_En selected.
[Profile]
id=t030_typo_token
name=T030 Typo Token No Crash
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGG,AGA
exclude=

[Scoring]
weight.chipset=100
