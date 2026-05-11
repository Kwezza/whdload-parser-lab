# T007: Exclude works even when weight=0 (scoring disabled for field).
# weight=0 means no score contribution from chipset, but exclude=AGA
# must still remove AGA variants before selection.
# GameB_v1.0_OCS_En must be selected; GameB_v1.0_AGA_En must not appear.
[Profile]
id=t007_exclude_zero_weight
name=T007 Exclude With Zero Weight
version=1
profile_format=1
debug=0

[Filter.chipset]
include=
exclude=AGA

[Scoring]
weight.chipset=0
