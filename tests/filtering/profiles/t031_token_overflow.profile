# T031: Include token list longer than the configured token limit (32).
# 34 tokens are specified.  The excess must be discarded with a warning;
# the profile must still load and filtering must continue.
# Expected: return code 0, warning present, no crash.
[Profile]
id=t031_token_overflow
name=T031 Include Token Overflow
version=1
profile_format=1
debug=0

[Filter.language]
include=A01,A02,A03,A04,A05,A06,A07,A08,A09,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28,A29,A30,A31,A32,A33,A34
exclude=

[Scoring]
weight.language=100
