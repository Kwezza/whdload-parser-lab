@echo off
rem tests/filtering/run_tests.bat
rem Stage J regression test runner for the TLV filtering harness.
rem
rem Usage: run_tests.bat [harness_exe]
rem   harness_exe  Path to filter_harness.exe (default: build\host\filter_harness.exe)
rem
rem Each test runs filter_harness and diffs the output against a golden file.
rem Exits with code 0 if all tests pass, 1 if any fail.
rem
rem Tests:
rem   T1  tiny_games.tlv + profile_aga_en     -> 5 selected, 3 rejected variants
rem   T2  tiny_games.tlv + profile_ocs_only   -> 2 selected, 3 rejected groups
rem   T3  tiny_games_fallback.tlv + aga_en    -> same result via heuristic grouping
rem   T4  CRC-mismatch detection              -> harness fails with missing/bad CSV
rem   T5  search "alien*" + aga_en            -> 1 selected (AlienBreed, prefix wildcard)
rem   T6  search "ALIEN" + aga_en             -> 1 selected (case-insensitive contains)
rem   T7  search "zzz_nomatch*" + aga_en      -> 0 selected, empty output, exit 0
rem   T8  search "*breed*" on fallback TLV    -> 1 selected via base_name heuristic
rem   T9  2-bucket chipset AGA/OCS            -> 7 variants, 2 lanes
rem   T10 2-bucket language En/De             -> 8 variants, 2 lanes
rem   T11 2-field slash (AGA/OCS x En/De)     -> 10 variants, 4 lanes
rem   T12 exclude=OCS beats include=AGA/OCS   -> 5 variants (OCS lane always empty)
rem   T13 duplicate suppression AGA/AGA,OCS   -> 10 variants, no duplicate filenames
rem   T14 search + slash profile              -> 2 variants (1 group, 2 lanes)
rem   T15 comma-only -> no "Selection lanes"  -> summary line absent
rem   T16 too many buckets (9 > 8 limit)      -> non-zero exit

setlocal enabledelayedexpansion

set "HARNESS=%~1"
if "%HARNESS%"=="" set "HARNESS=build\host\filter_harness.exe"
set "FH=%HARNESS%"

set "TESTS_DIR=tests\filtering"
set "OUT_DIR=build\host"
set "PASS_COUNT=0"
set "FAIL_COUNT=0"

echo ============================================================
echo  filter_harness regression tests
echo  Harness : %FH%
echo ============================================================
echo.

rem -------------------------------------------------------------------
rem T1: group_id TLV + AGA English profile
rem     Expected: 5 selected, 0 groups rejected
rem -------------------------------------------------------------------
echo [T1] tiny_games.tlv + profile_aga_en
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t1_out.txt" > "%OUT_DIR%\t1_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t1_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T2
)

fc /L "%OUT_DIR%\t1_out.txt" "%TESTS_DIR%\expected_aga_en.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t1_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_aga_en.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t1_summary.txt"

:T2
rem -------------------------------------------------------------------
rem T2: group_id TLV + OCS-only profile
rem     Expected: 2 selected, 3 groups rejected
rem -------------------------------------------------------------------
echo.
echo [T2] tiny_games.tlv + profile_ocs_only
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_ocs_only.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t2_out.txt" > "%OUT_DIR%\t2_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t2_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T3
)

fc /L "%OUT_DIR%\t2_out.txt" "%TESTS_DIR%\expected_ocs_only.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t2_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_ocs_only.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t2_summary.txt"

:T3
rem -------------------------------------------------------------------
rem T3: fallback TLV (no group_id) + AGA English profile
rem     Expected: same selection as T1 via display_name heuristic
rem -------------------------------------------------------------------
echo.
echo [T3] tiny_games_fallback.tlv + profile_aga_en  ^(heuristic grouping^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games_fallback.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --warn-crc ^
       --out "%OUT_DIR%\t3_out.txt" > "%OUT_DIR%\t3_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t3_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T4
)

fc /L "%OUT_DIR%\t3_out.txt" "%TESTS_DIR%\expected_aga_en.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t3_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_aga_en.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t3_summary.txt"

:T4
rem -------------------------------------------------------------------
rem T4: CRC mismatch detection (strict mode)
rem     Use a different defs dir whose CSV content differs from the TLV-
rem     embedded CRCs.  The harness must return non-zero in strict mode.
rem -------------------------------------------------------------------
echo.
echo [T4] CRC mismatch detection  ^(strict mode^)
rem Use the real assets_raw/defs; it has different content from the tiny
rem fixture CSVs so CRCs will not match.
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --defs "assets_raw\defs" ^
       --out "%OUT_DIR%\t4_out.txt" > "%OUT_DIR%\t4_summary.txt" 2>&1
set T4_EXIT=%errorlevel%

if %T4_EXIT% neq 0 (
    echo   PASS  ^(strict CRC correctly aborted with exit %T4_EXIT%^)
    set /a PASS_COUNT+=1
) else (
    echo   FAIL  ^(expected non-zero exit when CRCs mismatch in strict mode^)
    set /a FAIL_COUNT+=1
)
type "%OUT_DIR%\t4_summary.txt"

rem -------------------------------------------------------------------
rem T5: prefix wildcard search on group_id TLV
rem     --search alien* should match AlienBreed group only (group_map path)
rem     Expected: 1 selected, AlienBreed_v1.0_AGA_En
rem -------------------------------------------------------------------
echo.
echo [T5] search "alien*" on tiny_games.tlv + profile_aga_en  ^(prefix wildcard, group_map^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --search alien* ^
       --out "%OUT_DIR%\t5_out.txt" > "%OUT_DIR%\t5_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t5_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T6
)

fc /L "%OUT_DIR%\t5_out.txt" "%TESTS_DIR%\expected_search_alien_aga.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t5_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_search_alien_aga.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t5_summary.txt"

:T6
rem -------------------------------------------------------------------
rem T6: case-insensitive substring search (no wildcard) on group_id TLV
rem     --search ALIEN should match AlienBreed via ci_strstr, same result as T5
rem     Expected: 1 selected, AlienBreed_v1.0_AGA_En
rem -------------------------------------------------------------------
echo.
echo [T6] search "ALIEN" on tiny_games.tlv + profile_aga_en  ^(case-insensitive contains^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --search ALIEN ^
       --out "%OUT_DIR%\t6_out.txt" > "%OUT_DIR%\t6_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t6_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T7
)

fc /L "%OUT_DIR%\t6_out.txt" "%TESTS_DIR%\expected_search_alien_aga.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t6_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_search_alien_aga.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t6_summary.txt"

:T7
rem -------------------------------------------------------------------
rem T7: search with no matching groups
rem     Expected: exit 0, empty output, Selected: 0
rem -------------------------------------------------------------------
echo.
echo [T7] search "zzz_nomatch*" on tiny_games.tlv  ^(no match, exit 0^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --search zzz_nomatch* ^
       --out "%OUT_DIR%\t7_out.txt" > "%OUT_DIR%\t7_summary.txt" 2>&1
set T7_EXIT=%errorlevel%

if %T7_EXIT% neq 0 (
    echo   FAIL  ^(expected exit 0 on no-match search, got %T7_EXIT%^)
    type "%OUT_DIR%\t7_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T8
)

fc /L "%OUT_DIR%\t7_out.txt" "%TESTS_DIR%\expected_search_nomatch.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(expected empty output file^)
    echo   --- got ---
    type "%OUT_DIR%\t7_out.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t7_summary.txt"

:T8
rem -------------------------------------------------------------------
rem T8: wildcard search on fallback TLV (no group_id block)
rem     --search *breed* matches AlienBreed via base_name heuristic path
rem     Expected: 1 selected, AlienBreed_v1.0_AGA_En
rem -------------------------------------------------------------------
echo.
echo [T8] search "*breed*" on tiny_games_fallback.tlv + profile_aga_en  ^(base_name fallback^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games_fallback.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --warn-crc ^
       --search *breed* ^
       --out "%OUT_DIR%\t8_out.txt" > "%OUT_DIR%\t8_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t8_summary.txt"
    set /a FAIL_COUNT+=1
    goto :RESULTS
)

fc /L "%OUT_DIR%\t8_out.txt" "%TESTS_DIR%\expected_search_alien_aga.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t8_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_search_alien_aga.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t8_summary.txt"

:T9
rem -------------------------------------------------------------------
rem T9: 2-bucket chipset (AGA/OCS) + single language (En)
rem     Lane 0 selects best AGA, Lane 1 selects best OCS.
rem     Expected: 7 selected variants, 5 selected groups, 2 lanes.
rem -------------------------------------------------------------------
echo.
echo [T9] 2-bucket chipset AGA/OCS  (Selection lanes: 2)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_aga_ocs.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t9_out.txt" > "%OUT_DIR%\t9_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t9_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T10
)

fc /L "%OUT_DIR%\t9_out.txt" "%TESTS_DIR%\expected_bucket_aga_ocs.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t9_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_bucket_aga_ocs.txt"
    set /a FAIL_COUNT+=1
) else (
    findstr /C:"Selection lanes" "%OUT_DIR%\t9_summary.txt" > nul 2>&1
    if errorlevel 1 (
        echo   FAIL  ^(summary missing "Selection lanes" line^)
        type "%OUT_DIR%\t9_summary.txt"
        set /a FAIL_COUNT+=1
    ) else (
        echo   PASS
        set /a PASS_COUNT+=1
    )
)
type "%OUT_DIR%\t9_summary.txt"

:T10
rem -------------------------------------------------------------------
rem T10: 2-bucket language (En/De) + single chipset (AGA)
rem      Lane 0 selects best En variant, Lane 1 selects best De variant.
rem      Expected: 8 selected variants, 5 selected groups, 2 lanes.
rem -------------------------------------------------------------------
echo.
echo [T10] 2-bucket language En/De  (Selection lanes: 2)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_lang_en_de.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t10_out.txt" > "%OUT_DIR%\t10_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t10_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T11
)

fc /L "%OUT_DIR%\t10_out.txt" "%TESTS_DIR%\expected_bucket_lang_en_de.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t10_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_bucket_lang_en_de.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t10_summary.txt"

:T11
rem -------------------------------------------------------------------
rem T11: 2-field slash (AGA/OCS x En/De) -> 4 lanes (Cartesian product)
rem      Expected: 10 selected variants, 5 selected groups, 4 lanes.
rem -------------------------------------------------------------------
echo.
echo [T11] 2-field slash AGA/OCS x En/De  (Selection lanes: 4)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_2field.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t11_out.txt" > "%OUT_DIR%\t11_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t11_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T12
)

fc /L "%OUT_DIR%\t11_out.txt" "%TESTS_DIR%\expected_bucket_2field.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t11_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_bucket_2field.txt"
    set /a FAIL_COUNT+=1
) else (
    findstr /C:"Selection lanes" "%OUT_DIR%\t11_summary.txt" > nul 2>&1
    if errorlevel 1 (
        echo   FAIL  ^(summary missing "Selection lanes" line^)
        type "%OUT_DIR%\t11_summary.txt"
        set /a FAIL_COUNT+=1
    ) else (
        echo   PASS
        set /a PASS_COUNT+=1
    )
)
type "%OUT_DIR%\t11_summary.txt"

:T12
rem -------------------------------------------------------------------
rem T12: exclude wins over bucket (include=AGA/OCS but exclude=OCS)
rem      OCS variants are rejected in pre-pass; lane 1 always empty.
rem      Expected: same 5-line output as plain AGA profile (expected_aga_en.txt)
rem -------------------------------------------------------------------
echo.
echo [T12] exclude=OCS wins over include=AGA/OCS bucket
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_exclude_ocs.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t12_out.txt" > "%OUT_DIR%\t12_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t12_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T13
)

fc /L "%OUT_DIR%\t12_out.txt" "%TESTS_DIR%\expected_aga_en.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch - exclude should suppress OCS lane^)
    echo   --- got ---
    type "%OUT_DIR%\t12_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_aga_en.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t12_summary.txt"

:T13
rem -------------------------------------------------------------------
rem T13: duplicate suppression (include=AGA/AGA,OCS)
rem      Bucket 1 overlaps bucket 0; a variant cannot be selected twice,
rem      so the second lane falls back to the next-best candidate per group.
rem      Expected: 10 selected variants, 5 groups, 2 lanes, 0 rejected.
rem -------------------------------------------------------------------
echo.
echo [T13] duplicate suppression AGA/AGA,OCS
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_dup.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out "%OUT_DIR%\t13_out.txt" > "%OUT_DIR%\t13_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t13_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T14
)

fc /L "%OUT_DIR%\t13_out.txt" "%TESTS_DIR%\expected_bucket_dup.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t13_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_bucket_dup.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t13_summary.txt"

:T14
rem -------------------------------------------------------------------
rem T14: search interaction with slash profile
rem      --search alien* + 2-bucket chipset profile -> 1 matched group,
rem      2 selected variants (one per lane).
rem -------------------------------------------------------------------
echo.
echo [T14] search "alien*" with 2-bucket profile  ^(1 group, 2 lanes^)
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_aga_ocs.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --search alien* ^
       --out "%OUT_DIR%\t14_out.txt" > "%OUT_DIR%\t14_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t14_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T15
)

fc /L "%OUT_DIR%\t14_out.txt" "%TESTS_DIR%\expected_bucket_search_alien.txt" > nul 2>&1
if errorlevel 1 (
    echo   FAIL  ^(output mismatch^)
    echo   --- got ---
    type "%OUT_DIR%\t14_out.txt"
    echo   --- expected ---
    type "%TESTS_DIR%\expected_bucket_search_alien.txt"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS
    set /a PASS_COUNT+=1
)
type "%OUT_DIR%\t14_summary.txt"

:T15
rem -------------------------------------------------------------------
rem T15: no-slash profile produces no "Selection lanes" summary line
rem      Comma-only profile -> lane_count=1 -> summary line suppressed.
rem -------------------------------------------------------------------
echo.
echo [T15] comma-only profile -> no "Selection lanes" in summary
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_aga_en.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out nul > "%OUT_DIR%\t15_summary.txt" 2>&1

if errorlevel 1 (
    echo   FAIL  ^(harness returned non-zero^)
    type "%OUT_DIR%\t15_summary.txt"
    set /a FAIL_COUNT+=1
    goto :T16
)

findstr /C:"Selection lanes" "%OUT_DIR%\t15_summary.txt" > nul 2>&1
if errorlevel 1 (
    echo   PASS
    set /a PASS_COUNT+=1
) else (
    echo   FAIL  ^(comma-only profile must not print "Selection lanes" line^)
    type "%OUT_DIR%\t15_summary.txt"
    set /a FAIL_COUNT+=1
)
type "%OUT_DIR%\t15_summary.txt"

:T16
rem -------------------------------------------------------------------
rem T16: profile with too many buckets (9 > FP_MAX_BUCKETS_FIELD=8)
rem      profile load must fail; harness exits non-zero.
rem -------------------------------------------------------------------
echo.
echo [T16] too-many buckets (9 buckets in one field^) -> non-zero exit
"%FH%" --tlv "%TESTS_DIR%\tiny_games.tlv" ^
       --profile "%TESTS_DIR%\profile_bucket_overflow.profile" ^
       --defs "%TESTS_DIR%\defs" ^
       --out nul > "%OUT_DIR%\t16_summary.txt" 2>&1
set T16_EXIT=%errorlevel%

if %T16_EXIT% neq 0 (
    echo   PASS  ^(profile load correctly rejected with exit %T16_EXIT%^)
    set /a PASS_COUNT+=1
) else (
    echo   FAIL  ^(expected non-zero exit for 9-bucket profile, got 0^)
    set /a FAIL_COUNT+=1
)
type "%OUT_DIR%\t16_summary.txt"

:RESULTS
rem -------------------------------------------------------------------
echo.
echo ============================================================
echo  Results: %PASS_COUNT% passed, %FAIL_COUNT% failed
echo ============================================================

if %FAIL_COUNT% neq 0 exit /b 1
exit /b 0
