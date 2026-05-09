# Fixture profile: AGA with English preference
# Used by Stage J regression tests.
[Profile]
id=fixture_aga_en
name=Fixture AGA English
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA
exclude=OCS

[Filter.language]
include=En

[Scoring]
weight.chipset=10
weight.language=5
