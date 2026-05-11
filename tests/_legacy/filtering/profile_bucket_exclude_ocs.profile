# Fixture profile: exclude beats bucket include
# T12: chipset include=AGA/OCS but exclude=OCS -> OCS lane always empty
# Expected: same 5 lines as expected_aga_en.txt
[Profile]
id=fixture_bucket_exclude_ocs
name=Fixture exclude wins over bucket
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS
exclude=OCS

[Filter.language]
include=En

[Scoring]
weight.chipset=10
weight.language=5
