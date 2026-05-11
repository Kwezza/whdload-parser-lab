# T029: Profile with only unknown filter fields falls back to defaults.
# No valid [Filter.*] sections; the harness must load default filters and
# continue rather than aborting.
# Expected: return code 0, warning present, default-filter output produced.
[Profile]
id=t029_only_unknown
name=T029 Only Unknown Fields
version=1
profile_format=1
debug=0

[Filter.fakechipset]
include=AGA
exclude=

[Scoring]
weight.fakechipset=100
