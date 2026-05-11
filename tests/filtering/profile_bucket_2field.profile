# Fixture profile: 2-field slash (AGA/OCS x En/De) -> 4 lanes
# T11: Cartesian product of two slash fields.
# Expected: 10 selected variants, 5 selected groups, Selection lanes: 4
[Profile]
id=fixture_bucket_2field
name=Fixture 2-field slash AGA/OCS x En/De
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS

[Filter.language]
include=En/De

[Scoring]
weight.chipset=10
weight.language=5
