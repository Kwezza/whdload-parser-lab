# T028: Unknown filter field is ignored with warning.
# [Filter.fakechipset] must produce a warning and be skipped.
# [Filter.chipset] with include=OCS must still apply normally.
# Expected: return code 0, warning present in output, OCS variants selected.
[Profile]
id=t028_unknown_field
name=T028 Unknown Filter Field Ignored
version=1
profile_format=1
debug=0

[Filter.fakechipset]
include=AGA
exclude=

[Filter.chipset]
include=OCS
exclude=

[Scoring]
weight.fakechipset=100
weight.chipset=100
