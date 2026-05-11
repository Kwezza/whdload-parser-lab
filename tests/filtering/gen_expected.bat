@echo off
rem gen_expected.bat -- run from repo root to generate all Phase 3 expected files.
rem
rem Runs the filter harness once per test and writes the output (selected
rem variant names) to tests\filtering\expected\.  Also captures the full
rem stdout summary next to each expected file so the test runner can grep it.
rem
rem Usage: cmd /c tests\filtering\gen_expected.bat

set FH=build\host\filter_harness.exe
set TLV=tests\filtering\tlv\tiny_regression_games.tlv
set LFB=tests\filtering\tlv\tiny_legacy_no_group_map.tlv
set HI=tests\filtering\tlv\tiny_groupid_high.tlv
set MM=tests\filtering\tlv\tiny_crc_mismatch_base.tlv
set TRN=tests\filtering\tlv\tiny_bad_truncated.tlv
set DEFS=tests\filtering\defs
set PROF=tests\filtering\profiles
set EXP=tests\filtering\expected

if not exist "%EXP%" mkdir "%EXP%"

echo Generating expected outputs for T001-T027, T035, T037-T040, T042...
echo.

rem ---- Group A: Scoring priority ----------------------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t001_aga_priority.profile"       --defs "%DEFS%" --out "%EXP%\t001_out.txt" > "%EXP%\t001_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t002_language_priority.profile"   --defs "%DEFS%" --out "%EXP%\t002_out.txt" > "%EXP%\t002_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t003_lang_dominates.profile"      --defs "%DEFS%" --out "%EXP%\t003_out.txt" > "%EXP%\t003_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t004_chipset_dominates.profile"   --defs "%DEFS%" --out "%EXP%\t004_out.txt" > "%EXP%\t004_summary.txt" 2>&1

rem ---- Group B: Exclude semantics ----------------------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t005_exclude_aga.profile"         --defs "%DEFS%" --out "%EXP%\t005_out.txt" > "%EXP%\t005_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t006_cd32_excluded.profile"       --defs "%DEFS%" --out "%EXP%\t006_out.txt" > "%EXP%\t006_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t007_exclude_zero_weight.profile" --defs "%DEFS%" --out "%EXP%\t007_out.txt" > "%EXP%\t007_summary.txt" 2>&1

rem ---- Group C: Default token fallback -----------------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t008_ocs_preferred.profile"       --defs "%DEFS%" --out "%EXP%\t008_out.txt" > "%EXP%\t008_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t009_exclude_ocs.profile"         --defs "%DEFS%" --out "%EXP%\t009_out.txt" > "%EXP%\t009_summary.txt" 2>&1

rem ---- Group D: Tie-breaking (first in TLV wins) -------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t010_t011_tie.profile"            --defs "%DEFS%" --out "%EXP%\t010_out.txt" > "%EXP%\t010_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t010_t011_tie.profile"            --defs "%DEFS%" --out "%EXP%\t011_out.txt" > "%EXP%\t011_summary.txt" 2>&1

rem ---- Group E: Group-map vs fallback grouping ---------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --out "%EXP%\t012_out.txt" > "%EXP%\t012_summary.txt" 2>&1
"%FH%" --tlv "%LFB%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --warn-crc --out "%EXP%\t013_out.txt" > "%EXP%\t013_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --out "%EXP%\t014_out.txt" > "%EXP%\t014_summary.txt" 2>&1

rem ---- Group F: Search filter -------------------------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus*"  --out "%EXP%\t015_out.txt" > "%EXP%\t015_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "lotus*"  --out "%EXP%\t016_out.txt" > "%EXP%\t016_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus?"  --out "%EXP%\t017_out.txt" > "%EXP%\t017_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "LoTuS*"  --out "%EXP%\t018_out.txt" > "%EXP%\t018_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "zzznomatch" --out "%EXP%\t019_out.txt" > "%EXP%\t019_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus"    --out "%EXP%\t020_out.txt" > "%EXP%\t020_summary.txt" 2>&1

rem ---- Group G: Slash bucket selection lanes ----------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t021_t022_bucket_chipset.profile"  --defs "%DEFS%" --out "%EXP%\t021_out.txt" > "%EXP%\t021_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t021_t022_bucket_chipset.profile"  --defs "%DEFS%" --out "%EXP%\t022_out.txt" > "%EXP%\t022_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t023_bucket_missing.profile"        --defs "%DEFS%" --out "%EXP%\t023_out.txt" > "%EXP%\t023_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t024_exclude_global.profile"        --defs "%DEFS%" --out "%EXP%\t024_out.txt" > "%EXP%\t024_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t025_dup_suppression.profile"       --defs "%DEFS%" --out "%EXP%\t025_out.txt" > "%EXP%\t025_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t026_t027_cartesian.profile"        --defs "%DEFS%" --out "%EXP%\t026_out.txt" > "%EXP%\t026_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t026_t027_cartesian.profile"        --defs "%DEFS%" --out "%EXP%\t027_out.txt" > "%EXP%\t027_summary.txt" 2>&1

rem ---- Group H: Profile error handling ----------------------------------

rem T028-T031: return 0 + "Warnings: yes" in summary (no output to compare)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t028_unknown_field.profile"        --defs "%DEFS%" --out "%EXP%\t028_out.txt" > "%EXP%\t028_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t029_only_unknown.profile"         --defs "%DEFS%" --out "%EXP%\t029_out.txt" > "%EXP%\t029_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t030_typo_token.profile"           --defs "%DEFS%" --out "%EXP%\t030_out.txt" > "%EXP%\t030_summary.txt" 2>&1
"%FH%" --tlv "%TLV%" --profile "%PROF%\t031_token_overflow.profile"       --defs "%DEFS%" --out "%EXP%\t031_out.txt" > "%EXP%\t031_summary.txt" 2>&1
rem T032-T034: fatal (non-zero exit); no output file generated

rem ---- Group I: CRC validation ------------------------------------------

"%FH%" --tlv "%TLV%" --profile "%PROF%\t035_t037_crc.profile"             --defs "%DEFS%" --out "%EXP%\t035_out.txt" > "%EXP%\t035_summary.txt" 2>&1
rem T036: strict CRC mismatch -- non-zero exit; no output file
"%FH%" --tlv "%MM%"  --profile "%PROF%\t035_t037_crc.profile"             --defs "%DEFS%" --warn-crc --out "%EXP%\t037_out.txt" > "%EXP%\t037_summary.txt" 2>&1
rem T038: bad no fieldmap -- non-zero exit; no output file
"%FH%" --tlv "%TRN%" --profile "%PROF%\t035_t037_crc.profile"             --defs "%DEFS%" --warn-crc --out "%EXP%\t039_out.txt" > "%EXP%\t039_summary.txt" 2>&1

rem ---- Group J: Endian correctness ---------------------------------------

"%FH%" --tlv "%HI%"  --profile "%PROF%\t040_t042_endian.profile"          --defs "%DEFS%" --out "%EXP%\t040_out.txt" > "%EXP%\t040_summary.txt" 2>&1
rem T041: SKIPPED (Amiga-only endian test)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t040_t042_endian.profile"          --defs "%DEFS%" --out "%EXP%\t042_out.txt" > "%EXP%\t042_summary.txt" 2>&1

echo Done. Files written to %EXP%\
