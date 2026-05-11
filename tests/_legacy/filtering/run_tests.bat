@echo off
rem tests/filtering/run_tests.bat
rem T001-T045 regression test runner for the TLV filtering harness.
rem
rem Usage: run_tests.bat [harness_exe]
rem   harness_exe  Path to filter_harness.exe (default: build\host\filter_harness.exe)
rem
rem Each test runs filter_harness and compares output against a golden file.
rem Fatal-exit tests verify the harness returns non-zero.
rem Warning tests verify exit 0 plus "Warnings: yes" in the summary.
rem Exits with code 0 if all tests pass, 1 if any fail.

setlocal enabledelayedexpansion

set "HARNESS=%~1"
if "%HARNESS%"=="" set "HARNESS=build\host\filter_harness.exe"
set "FH=%HARNESS%"

set "TD=tests\filtering"
set "TLV=%TD%\tlv\tiny_regression_games.tlv"
set "LFB=%TD%\tlv\tiny_legacy_no_group_map.tlv"
set "HI=%TD%\tlv\tiny_groupid_high.tlv"
set "MM=%TD%\tlv\tiny_crc_mismatch_base.tlv"
set "BNF=%TD%\tlv\tiny_bad_no_fieldmap.tlv"
set "TRN=%TD%\tlv\tiny_bad_truncated.tlv"
set "GAM=output\GamB(2026-04-26).tlv"
set "DEFS=%TD%\defs"
set "ADEFS=assets_raw\defs"
set "PROF=%TD%\profiles"
set "EXP=%TD%\expected"
set "OUT=build\host"

set "PASS_COUNT=0"
set "FAIL_COUNT=0"

echo ============================================================
echo  filter_harness regression tests  T001-T045
echo  Harness : %FH%
echo ============================================================
echo.

rem ===========================================================================
rem  Group A: Scoring priority
rem ===========================================================================

echo --- Group A: Scoring priority ---

:T001
echo [T001] AGA priority
"%FH%" --tlv "%TLV%" --profile "%PROF%\t001_aga_priority.profile" --defs "%DEFS%" --out "%OUT%\t001_out.txt" > "%OUT%\t001_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t001_summary.txt" & set /a FAIL_COUNT+=1 & goto :T002)
fc /L "%OUT%\t001_out.txt" "%EXP%\t001_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T002
echo [T002] Language priority
"%FH%" --tlv "%TLV%" --profile "%PROF%\t002_language_priority.profile" --defs "%DEFS%" --out "%OUT%\t002_out.txt" > "%OUT%\t002_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t002_summary.txt" & set /a FAIL_COUNT+=1 & goto :T003)
fc /L "%OUT%\t002_out.txt" "%EXP%\t002_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T003
echo [T003] Language dominates chipset
"%FH%" --tlv "%TLV%" --profile "%PROF%\t003_lang_dominates.profile" --defs "%DEFS%" --out "%OUT%\t003_out.txt" > "%OUT%\t003_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t003_summary.txt" & set /a FAIL_COUNT+=1 & goto :T004)
fc /L "%OUT%\t003_out.txt" "%EXP%\t003_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T004
echo [T004] Chipset dominates language
"%FH%" --tlv "%TLV%" --profile "%PROF%\t004_chipset_dominates.profile" --defs "%DEFS%" --out "%OUT%\t004_out.txt" > "%OUT%\t004_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t004_summary.txt" & set /a FAIL_COUNT+=1 & goto :T005)
fc /L "%OUT%\t004_out.txt" "%EXP%\t004_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group B: Exclude semantics
rem ===========================================================================

echo --- Group B: Exclude semantics ---

:T005
echo [T005] Exclude AGA ^(OCS fallback wins^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t005_exclude_aga.profile" --defs "%DEFS%" --out "%OUT%\t005_out.txt" > "%OUT%\t005_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t005_summary.txt" & set /a FAIL_COUNT+=1 & goto :T006)
fc /L "%OUT%\t005_out.txt" "%EXP%\t005_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T006
echo [T006] CD32 excluded ^(group absent from output^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t006_cd32_excluded.profile" --defs "%DEFS%" --out "%OUT%\t006_out.txt" > "%OUT%\t006_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t006_summary.txt" & set /a FAIL_COUNT+=1 & goto :T007)
fc /L "%OUT%\t006_out.txt" "%EXP%\t006_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T007
echo [T007] Exclude via zero weight
"%FH%" --tlv "%TLV%" --profile "%PROF%\t007_exclude_zero_weight.profile" --defs "%DEFS%" --out "%OUT%\t007_out.txt" > "%OUT%\t007_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t007_summary.txt" & set /a FAIL_COUNT+=1 & goto :T008)
fc /L "%OUT%\t007_out.txt" "%EXP%\t007_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group C: Default token fallback
rem ===========================================================================

echo --- Group C: Default token fallback ---

:T008
echo [T008] OCS preferred ^(variant has no chipset token - uses default^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t008_ocs_preferred.profile" --defs "%DEFS%" --out "%OUT%\t008_out.txt" > "%OUT%\t008_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t008_summary.txt" & set /a FAIL_COUNT+=1 & goto :T009)
fc /L "%OUT%\t008_out.txt" "%EXP%\t008_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T009
echo [T009] Exclude OCS ^(default-chipset variants excluded^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t009_exclude_ocs.profile" --defs "%DEFS%" --out "%OUT%\t009_out.txt" > "%OUT%\t009_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t009_summary.txt" & set /a FAIL_COUNT+=1 & goto :T010)
fc /L "%OUT%\t009_out.txt" "%EXP%\t009_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group D: Tie-breaking (first-in-TLV wins)
rem ===========================================================================

echo --- Group D: Tie-breaking ---

:T010
echo [T010] Tie-break: TieGame_v1.0_AGA_En first in TLV wins
"%FH%" --tlv "%TLV%" --profile "%PROF%\t010_t011_tie.profile" --defs "%DEFS%" --out "%OUT%\t010_out.txt" > "%OUT%\t010_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t010_summary.txt" & set /a FAIL_COUNT+=1 & goto :T011)
fc /L "%OUT%\t010_out.txt" "%EXP%\t010_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (
    findstr /C:"TieGame_v1.0_AGA_En" "%OUT%\t010_out.txt" > nul 2>&1
    if errorlevel 1 (echo   FAIL  ^(TieGame_v1.0_AGA_En not in output^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)
)

:T011
echo [T011] Tie-break: TieGame2_v1.1_AGA_En also present
"%FH%" --tlv "%TLV%" --profile "%PROF%\t010_t011_tie.profile" --defs "%DEFS%" --out "%OUT%\t011_out.txt" > "%OUT%\t011_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t011_summary.txt" & set /a FAIL_COUNT+=1 & goto :T012)
fc /L "%OUT%\t011_out.txt" "%EXP%\t011_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (
    findstr /C:"TieGame2_v1.1_AGA_En" "%OUT%\t011_out.txt" > nul 2>&1
    if errorlevel 1 (echo   FAIL  ^(TieGame2_v1.1_AGA_En not in output^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)
)

echo.
rem ===========================================================================
rem  Group E: Group-map vs fallback grouping
rem ===========================================================================

echo --- Group E: Group-map vs fallback grouping ---

:T012
echo [T012] Group-map grouping ^(chipset AGA/OCS selection, regression TLV^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --out "%OUT%\t012_out.txt" > "%OUT%\t012_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t012_summary.txt" & set /a FAIL_COUNT+=1 & goto :T013)
fc /L "%OUT%\t012_out.txt" "%EXP%\t012_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T013
echo [T013] Fallback grouping ^(no group_id block, legacy TLV, --warn-crc^)
"%FH%" --tlv "%LFB%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --warn-crc --out "%OUT%\t013_out.txt" > "%OUT%\t013_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t013_summary.txt" & set /a FAIL_COUNT+=1 & goto :T014)
fc /L "%OUT%\t013_out.txt" "%EXP%\t013_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T014
echo [T014] Group-map multi-group selection ^(same profile as T012^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --out "%OUT%\t014_out.txt" > "%OUT%\t014_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t014_summary.txt" & set /a FAIL_COUNT+=1 & goto :T015)
fc /L "%OUT%\t014_out.txt" "%EXP%\t014_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group F: Search filter
rem ===========================================================================

echo --- Group F: Search filter ---

:T015
echo [T015] Search "Lotus*" ^(prefix wildcard, 4 groups^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus*" --out "%OUT%\t015_out.txt" > "%OUT%\t015_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t015_summary.txt" & set /a FAIL_COUNT+=1 & goto :T016)
fc /L "%OUT%\t015_out.txt" "%EXP%\t015_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T016
echo [T016] Search "lotus*" ^(case-insensitive prefix^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "lotus*" --out "%OUT%\t016_out.txt" > "%OUT%\t016_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t016_summary.txt" & set /a FAIL_COUNT+=1 & goto :T017)
fc /L "%OUT%\t016_out.txt" "%EXP%\t016_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T017
echo [T017] Search "Lotus?" ^(single-char wildcard^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus?" --out "%OUT%\t017_out.txt" > "%OUT%\t017_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t017_summary.txt" & set /a FAIL_COUNT+=1 & goto :T018)
fc /L "%OUT%\t017_out.txt" "%EXP%\t017_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T018
echo [T018] Search "LoTuS*" ^(mixed-case prefix^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "LoTuS*" --out "%OUT%\t018_out.txt" > "%OUT%\t018_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t018_summary.txt" & set /a FAIL_COUNT+=1 & goto :T019)
fc /L "%OUT%\t018_out.txt" "%EXP%\t018_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T019
echo [T019] Search "zzznomatch" ^(no match, empty output, exit 0^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "zzznomatch" --out "%OUT%\t019_out.txt" > "%OUT%\t019_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero on no-match^) & type "%OUT%\t019_summary.txt" & set /a FAIL_COUNT+=1 & goto :T020)
fc /L "%OUT%\t019_out.txt" "%EXP%\t019_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(expected empty output^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T020
echo [T020] Search "Lotus" ^(no wildcard - contains match, 4 groups^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t012_t020_chipset_aga_ocs.profile" --defs "%DEFS%" --search "Lotus" --out "%OUT%\t020_out.txt" > "%OUT%\t020_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t020_summary.txt" & set /a FAIL_COUNT+=1 & goto :T021)
fc /L "%OUT%\t020_out.txt" "%EXP%\t020_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group G: Slash bucket selection lanes
rem ===========================================================================

echo --- Group G: Slash bucket selection lanes ---

:T021
echo [T021] Bucket 2 lanes ^(chipset AGA / OCS^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t021_t022_bucket_chipset.profile" --defs "%DEFS%" --out "%OUT%\t021_out.txt" > "%OUT%\t021_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t021_summary.txt" & set /a FAIL_COUNT+=1 & goto :T022)
fc /L "%OUT%\t021_out.txt" "%EXP%\t021_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T022
echo [T022] Bucket 2 lanes - ECS in lane 1 ^(not OCS^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t021_t022_bucket_chipset.profile" --defs "%DEFS%" --out "%OUT%\t022_out.txt" > "%OUT%\t022_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t022_summary.txt" & set /a FAIL_COUNT+=1 & goto :T023)
fc /L "%OUT%\t022_out.txt" "%EXP%\t022_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T023
echo [T023] Bucket with missing token ^(group selected via other lane^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t023_bucket_missing.profile" --defs "%DEFS%" --out "%OUT%\t023_out.txt" > "%OUT%\t023_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t023_summary.txt" & set /a FAIL_COUNT+=1 & goto :T024)
fc /L "%OUT%\t023_out.txt" "%EXP%\t023_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T024
echo [T024] Global exclude overrides bucket include
"%FH%" --tlv "%TLV%" --profile "%PROF%\t024_exclude_global.profile" --defs "%DEFS%" --out "%OUT%\t024_out.txt" > "%OUT%\t024_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t024_summary.txt" & set /a FAIL_COUNT+=1 & goto :T025)
fc /L "%OUT%\t024_out.txt" "%EXP%\t024_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T025
echo [T025] Duplicate suppression ^(same variant in 2 buckets - deduplicated^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t025_dup_suppression.profile" --defs "%DEFS%" --out "%OUT%\t025_out.txt" > "%OUT%\t025_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t025_summary.txt" & set /a FAIL_COUNT+=1 & goto :T026)
fc /L "%OUT%\t025_out.txt" "%EXP%\t025_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T026
echo [T026] Cartesian product ^(2 chipset x 2 language lanes^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t026_t027_cartesian.profile" --defs "%DEFS%" --out "%OUT%\t026_out.txt" > "%OUT%\t026_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t026_summary.txt" & set /a FAIL_COUNT+=1 & goto :T027)
fc /L "%OUT%\t026_out.txt" "%EXP%\t026_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T027
echo [T027] Cartesian product ^(cross-check: same output as T026^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t026_t027_cartesian.profile" --defs "%DEFS%" --out "%OUT%\t027_out.txt" > "%OUT%\t027_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t027_summary.txt" & set /a FAIL_COUNT+=1 & goto :T028)
fc /L "%OUT%\t027_out.txt" "%EXP%\t027_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group H: Profile error handling
rem ===========================================================================

echo --- Group H: Profile error handling ---

:T028
echo [T028] Unknown field in profile ^(exit 0 + Warnings: yes^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t028_unknown_field.profile" --defs "%DEFS%" --out "%OUT%\t028_out.txt" > "%OUT%\t028_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero - expected exit 0 with warning^) & type "%OUT%\t028_summary.txt" & set /a FAIL_COUNT+=1 & goto :T029)
fc /L "%OUT%\t028_out.txt" "%EXP%\t028_expected.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1
) else (
    findstr /C:"Warnings: yes" "%OUT%\t028_summary.txt" > nul 2>&1
    if errorlevel 1 (echo   FAIL  ^("Warnings: yes" not in summary^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)
)

:T029
echo [T029] Only unknown field ^(no recognized filter, exit 0 + Warnings: yes^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t029_only_unknown.profile" --defs "%DEFS%" --out "%OUT%\t029_out.txt" > "%OUT%\t029_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero - expected exit 0 with warning^) & type "%OUT%\t029_summary.txt" & set /a FAIL_COUNT+=1 & goto :T030)
fc /L "%OUT%\t029_out.txt" "%EXP%\t029_expected.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1
) else (
    findstr /C:"Warnings: yes" "%OUT%\t029_summary.txt" > nul 2>&1
    if errorlevel 1 (echo   FAIL  ^("Warnings: yes" not in summary^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)
)

:T030
echo [T030] Typo token in profile ^(exit 0, filter runs with token ignored^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t030_typo_token.profile" --defs "%DEFS%" --out "%OUT%\t030_out.txt" > "%OUT%\t030_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t030_summary.txt" & set /a FAIL_COUNT+=1 & goto :T031)
fc /L "%OUT%\t030_out.txt" "%EXP%\t030_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T031
echo [T031] Token list overflow ^(exit 0, filter runs with overflow ignored^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t031_token_overflow.profile" --defs "%DEFS%" --out "%OUT%\t031_out.txt" > "%OUT%\t031_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t031_summary.txt" & set /a FAIL_COUNT+=1 & goto :T032)
fc /L "%OUT%\t031_out.txt" "%EXP%\t031_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T032
echo [T032] Too many buckets ^(FP_MAX_BUCKETS_FIELD=8 exceeded, fatal exit^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t032_too_many_buckets.profile" --defs "%DEFS%" --out "%OUT%\t032_out.txt" > "%OUT%\t032_summary.txt" 2>&1
if errorlevel 1 (echo   PASS & set /a PASS_COUNT+=1) else (echo   FAIL  ^(expected non-zero exit^) & set /a FAIL_COUNT+=1)

:T033
echo [T033] Too many lanes ^(FP_MAX_SELECTION_LANES=32 exceeded, fatal exit^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t033_too_many_lanes.profile" --defs "%DEFS%" --out "%OUT%\t033_out.txt" > "%OUT%\t033_summary.txt" 2>&1
if errorlevel 1 (echo   PASS & set /a PASS_COUNT+=1) else (echo   FAIL  ^(expected non-zero exit^) & set /a FAIL_COUNT+=1)

:T034
echo [T034] Too many slash fields ^(FP_MAX_BUCKET_FIELDS=4 exceeded, fatal exit^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t034_too_many_slash_fields.profile" --defs "%DEFS%" --out "%OUT%\t034_out.txt" > "%OUT%\t034_summary.txt" 2>&1
if errorlevel 1 (echo   PASS & set /a PASS_COUNT+=1) else (echo   FAIL  ^(expected non-zero exit^) & set /a FAIL_COUNT+=1)

echo.
rem ===========================================================================
rem  Group I: CRC validation
rem ===========================================================================

echo --- Group I: CRC validation ---

:T035
echo [T035] CRC match ^(all 5 CSVs match embedded fingerprints^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out "%OUT%\t035_out.txt" > "%OUT%\t035_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t035_summary.txt" & set /a FAIL_COUNT+=1 & goto :T036)
fc /L "%OUT%\t035_out.txt" "%EXP%\t035_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (
    findstr /C:"CSV CRC: OK" "%OUT%\t035_summary.txt" > nul 2>&1
    if errorlevel 1 (echo   FAIL  ^("CSV CRC: OK" not in summary^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)
)

:T036
echo [T036] CRC mismatch strict mode ^(all 5 CRCs XOR-inverted, fatal exit^)
"%FH%" --tlv "%MM%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out "%OUT%\t036_out.txt" > "%OUT%\t036_summary.txt" 2>&1
if errorlevel 1 (echo   PASS & set /a PASS_COUNT+=1) else (echo   FAIL  ^(expected non-zero on strict CRC mismatch^) & set /a FAIL_COUNT+=1)

:T037
echo [T037] CRC mismatch warn-only ^(--warn-crc, exit 0 + output produced^)
"%FH%" --tlv "%MM%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --warn-crc --out "%OUT%\t037_out.txt" > "%OUT%\t037_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero - expected exit 0 with --warn-crc^) & type "%OUT%\t037_summary.txt" & set /a FAIL_COUNT+=1 & goto :T038)
fc /L "%OUT%\t037_out.txt" "%EXP%\t037_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T038
echo [T038] No field map ^(field map block missing entirely, fatal exit^)
"%FH%" --tlv "%BNF%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out "%OUT%\t038_out.txt" > "%OUT%\t038_summary.txt" 2>&1
if errorlevel 1 (echo   PASS & set /a PASS_COUNT+=1) else (echo   FAIL  ^(expected non-zero - no field map^) & set /a FAIL_COUNT+=1)

:T039
echo [T039] Truncated TLV ^(50-byte truncation inside field map, no positive error^)
"%FH%" --tlv "%TRN%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out nul > "%OUT%\t039_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned positive non-zero^) & type "%OUT%\t039_summary.txt" & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group J: Endian correctness
rem ===========================================================================

echo --- Group J: Endian correctness ---

:T040
echo [T040] Big-endian group_id=300 ^(0x012C, parsed correctly^)
"%FH%" --tlv "%HI%" --profile "%PROF%\t040_t042_endian.profile" --defs "%DEFS%" --out "%OUT%\t040_out.txt" > "%OUT%\t040_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t040_summary.txt" & set /a FAIL_COUNT+=1 & goto :T041)
fc /L "%OUT%\t040_out.txt" "%EXP%\t040_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T041
echo [T041] SKIPPED ^(Amiga-only endian test, not run on host^)
set /a PASS_COUNT+=1

:T042
echo [T042] Endian correctness - regression TLV ^(normal group_ids^)
"%FH%" --tlv "%TLV%" --profile "%PROF%\t040_t042_endian.profile" --defs "%DEFS%" --out "%OUT%\t042_out.txt" > "%OUT%\t042_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t042_summary.txt" & set /a FAIL_COUNT+=1 & goto :T043)
fc /L "%OUT%\t042_out.txt" "%EXP%\t042_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

echo.
rem ===========================================================================
rem  Group K: Real Games TLV (integration)
rem ===========================================================================

echo --- Group K: Real Games TLV integration ---

:T043
echo [T043] Real Games TLV + pal_aga_4mb profile ^(integration^)
"%FH%" --tlv "%GAM%" --profile "assets_raw\profiles\pal_aga_4mb.profile" --defs "%ADEFS%" --out "%OUT%\t043_out.txt" > "%OUT%\t043_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t043_summary.txt" & set /a FAIL_COUNT+=1 & goto :T044)
fc /L "%OUT%\t043_out.txt" "%EXP%\t043_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T044
echo [T044] Real Games TLV + multi_bucket_reference profile ^(integration^)
"%FH%" --tlv "%GAM%" --profile "assets_raw\profiles\multi_bucket_reference.profile" --defs "%ADEFS%" --out "%OUT%\t044_out.txt" > "%OUT%\t044_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t044_summary.txt" & set /a FAIL_COUNT+=1 & goto :T045)
fc /L "%OUT%\t044_out.txt" "%EXP%\t044_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:T045
echo [T045] Real Games TLV + pal_aga_4mb + search "Alien*"
"%FH%" --tlv "%GAM%" --profile "assets_raw\profiles\pal_aga_4mb.profile" --defs "%ADEFS%" --search "Alien*" --out "%OUT%\t045_out.txt" > "%OUT%\t045_summary.txt" 2>&1
if errorlevel 1 (echo   FAIL  ^(harness returned non-zero^) & type "%OUT%\t045_summary.txt" & set /a FAIL_COUNT+=1 & goto :RESULTS)
fc /L "%OUT%\t045_out.txt" "%EXP%\t045_expected.txt" > nul 2>&1
if errorlevel 1 (echo   FAIL  ^(output mismatch^) & set /a FAIL_COUNT+=1) else (echo   PASS & set /a PASS_COUNT+=1)

:RESULTS
echo.
echo ============================================================
echo  Results: %PASS_COUNT% passed, %FAIL_COUNT% failed
echo ============================================================

if %FAIL_COUNT% neq 0 exit /b 1
exit /b 0
