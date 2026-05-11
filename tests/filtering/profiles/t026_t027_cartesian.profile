# T026 / T027: Cartesian product; two slash fields -> four lanes.
# chipset {AGA} x {OCS}  x  language {En} x {De}  =  4 lanes
# T026 fixture has all four combos -> 4 variants selected.
# T027 fixture only has AGA+En and OCS+De -> 2 variants, 2 skipped lanes.
[Profile]
id=t026_t027_cartesian
name=T026/T027 Cartesian Product Two Fields
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS
exclude=

[Filter.language]
include=En/De
exclude=

[Scoring]
weight.chipset=100
weight.language=100
