# T001: AGA beats ECS beats OCS
# include list is ordered left-to-right by preference; AGA ranks highest.
[Profile]
id=t001_aga_priority
name=T001 AGA Priority
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,ECS,OCS
exclude=

[Scoring]
weight.chipset=100
