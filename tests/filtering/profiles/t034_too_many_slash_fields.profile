# T034: More than the allowed number of slash-enabled fields is fatal.
# FP_MAX_BUCKET_FIELDS=4; this profile uses 5 fields with / in include.
# Expected: harness returns non-zero, error mentions bucket field limit.
[Profile]
id=t034_too_many_slash_fields
name=T034 Too Many Slash-Enabled Fields
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA/OCS
exclude=

[Filter.language]
include=En/De
exclude=

[Filter.memory]
include=1MB/512KB
exclude=

[Filter.video]
include=PAL/NTSC
exclude=

[Filter.media]
include=Disk/CD
exclude=

[Scoring]
weight.chipset=100
weight.language=100
weight.memory=100
weight.video=100
weight.media=100
