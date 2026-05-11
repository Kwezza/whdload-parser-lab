# T010 / T011: Equal-score tie broken by TLV encounter order.
# Both chipset and language are weighted equally.  Variants with AGA+En
# score the same; the one that appears first in the TLV must win.
# T010 fixture has v1.0 first -> v1.0 wins.
# T011 fixture has v1.1 first -> v1.1 wins.
[Profile]
id=t010_t011_tie
name=T010/T011 Equal Score Tie-Breaker
version=1
profile_format=1
debug=0

[Filter.chipset]
include=AGA
exclude=

[Filter.language]
include=En
exclude=

[Scoring]
weight.chipset=100
weight.language=100
