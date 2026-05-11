# T009: Default token can be excluded.
# DefaultExcluded_v1.0_En inherits OCS as the CSV default chipset.
# With exclude=OCS that variant is removed before selection, leaving
# DefaultExcluded_v1.0_AGA_En as the only eligible variant.
[Profile]
id=t009_exclude_ocs
name=T009 Exclude Default Token
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=OCS

[Scoring]
weight.chipset=100
