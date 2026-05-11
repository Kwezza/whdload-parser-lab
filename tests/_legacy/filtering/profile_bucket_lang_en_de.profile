# Fixture profile: 2-bucket language En/De, chipset AGA only
# T10: language slash produces 2 selection lanes.
# Expected: 7 selected variants, 5 selected groups, Selection lanes: 2
[Profile]
id=fixture_bucket_lang_en_de
name=Fixture 2-bucket Language En/De
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA

[Filter.language]
include=En/De

[Scoring]
weight.chipset=10
weight.language=5
