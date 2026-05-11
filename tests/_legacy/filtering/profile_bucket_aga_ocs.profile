# Fixture profile: 2-bucket chipset AGA/OCS, language En
# T9: chipset slash produces 2 selection lanes.
# Expected: 7 selected variants, 5 selected groups, Selection lanes: 2
[Profile]
id=fixture_bucket_aga_ocs
name=Fixture 2-bucket Chipset AGA/OCS
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS

[Filter.language]
include=En

[Scoring]
weight.chipset=10
weight.language=5
