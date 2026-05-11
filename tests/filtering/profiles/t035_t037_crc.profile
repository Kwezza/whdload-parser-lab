# T035 / T036 / T037: CRC validation tests (shared profile).
# T035: matching CRC on valid TLV -> CSV CRC: OK
# T036: same profile + tiny_crc_mismatch_base.tlv + --strict-crc -> non-zero exit
# T037: same profile + tiny_crc_mismatch_base.tlv + --warn-crc  -> exit 0, warning
[Profile]
id=t035_t037_crc
name=T035-T037 CRC Validation
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA,OCS
exclude=

[Scoring]
weight.chipset=100
