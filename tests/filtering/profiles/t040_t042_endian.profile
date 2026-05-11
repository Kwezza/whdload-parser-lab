# T040 / T042: Endian correctness tests (shared profile).
# T040: group_id=300 (0x012C) must be read as big-endian uint16.
#       GroupHigh_v1.0_AGA_En must win (AGA ranks above OCS).
# T042: token IDs must be read as little-endian uint32 even though group_id
#       and archive_info are big-endian.  Scoring must produce AGA+En winner.
[Profile]
id=t040_t042_endian
name=T040/T042 Endian Correctness
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=

[Filter.language]
include=En,De
exclude=

[Scoring]
weight.chipset=100
weight.language=100
