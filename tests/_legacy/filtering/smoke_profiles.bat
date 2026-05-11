@echo off
rem Temporary smoke-test for phase 2 profile files.
rem Run from repo root: tests\filtering\smoke_profiles.bat
set FH=build\host\filter_harness.exe
set TLV=tests\filtering\tlv\tiny_regression_games.tlv
set DEFS=tests\filtering\defs
set PROF=tests\filtering\profiles
set PASS=0
set FAIL=0

echo === T001: AGA priority (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t001_aga_priority.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T002: language priority (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t002_language_priority.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T003: lang dominates (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t003_lang_dominates.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T021: bucket 2 lanes (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t021_t022_bucket_chipset.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T026: cartesian product (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t026_t027_cartesian.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T028: unknown field (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t028_unknown_field.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T029: only unknown field (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t029_only_unknown.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T030: typo token (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t030_typo_token.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T031: token overflow (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t031_token_overflow.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T032: too many buckets (expect non-zero) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t032_too_many_buckets.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo PASS & set /a PASS+=1) else (echo FAIL & set /a FAIL+=1)

echo === T033: too many lanes (expect non-zero) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t033_too_many_lanes.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo PASS & set /a PASS+=1) else (echo FAIL & set /a FAIL+=1)

echo === T034: too many slash fields (expect non-zero) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t034_too_many_slash_fields.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo PASS & set /a PASS+=1) else (echo FAIL & set /a FAIL+=1)

echo === T035: CRC match (expect exit 0) ===
"%FH%" --tlv "%TLV%" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T036: CRC mismatch strict (expect non-zero) ===
"%FH%" --tlv "tests\filtering\tlv\tiny_crc_mismatch_base.tlv" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --strict-crc --out nul
if errorlevel 1 (echo PASS & set /a PASS+=1) else (echo FAIL & set /a FAIL+=1)

echo === T037: CRC mismatch warn-only (expect exit 0) ===
"%FH%" --tlv "tests\filtering\tlv\tiny_crc_mismatch_base.tlv" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --warn-crc --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T038: no field map (expect non-zero) ===
"%FH%" --tlv "tests\filtering\tlv\tiny_bad_no_fieldmap.tlv" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo PASS & set /a PASS+=1) else (echo FAIL & set /a FAIL+=1)

echo === T039: truncated TLV (expect exit 0 -- survives gracefully with warning) ===
"%FH%" --tlv "tests\filtering\tlv\tiny_bad_truncated.tlv" --profile "%PROF%\t035_t037_crc.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo === T040: big-endian group_id=300 (expect exit 0) ===
"%FH%" --tlv "tests\filtering\tlv\tiny_groupid_high.tlv" --profile "%PROF%\t040_t042_endian.profile" --defs "%DEFS%" --out nul
if errorlevel 1 (echo FAIL & set /a FAIL+=1) else (echo PASS & set /a PASS+=1)

echo.
echo ============================================================
echo  Smoke results: %PASS% passed, %FAIL% failed
echo ============================================================
if %FAIL% gtr 0 exit /b 1
exit /b 0
