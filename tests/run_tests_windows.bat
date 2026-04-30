@echo off
setlocal

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul 2>&1
if errorlevel 1 (
    echo Failed to change directory to "%ROOT%"
    exit /b 1
)

set "HARNESS=variant_harness.exe"
if not exist "%HARNESS%" set "HARNESS=variant_harness"
if not exist "%HARNESS%" (
    echo variant_harness executable not found. Run "make test-build" first.
    popd >nul
    exit /b 1
)

set "STRING_TEST=tests\vh_string_pool_test.exe"
if not exist "%STRING_TEST%" set "STRING_TEST=tests\vh_string_pool_test"
if not exist "%STRING_TEST%" (
    echo String pool test executable not found. Run "make test-build" first.
    popd >nul
    exit /b 1
)

set "PARSE_TEST=tests\vh_parse_group_key_test.exe"
if not exist "%PARSE_TEST%" set "PARSE_TEST=tests\vh_parse_group_key_test"
if not exist "%PARSE_TEST%" (
    echo Parse group key test executable not found. Run "make test-build" first.
    popd >nul
    exit /b 1
)

echo Running unit tests...
"%STRING_TEST%"
if errorlevel 1 goto :fail

"%PARSE_TEST%"
if errorlevel 1 goto :fail

echo Running milestone regression script...
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_milestone4_tests.ps1
if errorlevel 1 goto :fail

echo All tests passed.
popd >nul
exit /b 0

:fail
echo Tests failed.
popd >nul
exit /b 1
