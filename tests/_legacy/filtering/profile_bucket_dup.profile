# Fixture profile: duplicate suppression test
# T13: bucket1 overlaps bucket0 (AGA in both); the same variant cannot be
# selected twice, so the second lane falls back to the next-best candidate.
# Expected: 10 selected variants, 5 selected groups, Selection lanes: 2
[Profile]
id=fixture_bucket_dup
name=Fixture duplicate suppression AGA/AGA,OCS
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/AGA,OCS

[Filter.language]
include=En

[Scoring]
weight.chipset=10
weight.language=5
