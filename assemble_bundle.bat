@echo off
rem assemble_bundle.bat
rem
rem Assembles a self-contained Amiga test bundle in amiga_bundle\.
rem Mount amiga_bundle\ in WinUAE as a read-write directory hard drive.
rem
rem From the Amiga CLI:
rem   STACK 100000
rem   cd DH1:
rem   test_amiga_endian >output/amiga_results.txt
rem
rem All copies use xcopy /B (binary mode) to preserve CRLF line endings
rem in the CSV files.  Do NOT convert line endings -- the CRC validator
rem expects CRLF and any conversion will cause T13 to fail.
rem
rem Requires: build\amiga\test_amiga_endian must exist.
rem Run:      make TARGET=amiga test-amiga-endian   (to build the binary)
rem

setlocal

set BUNDLE=amiga_bundle
set AMIGA_BIN=build\amiga\test_amiga_endian

if not exist "%AMIGA_BIN%" (
    echo ERROR: %AMIGA_BIN% not found.
    echo Run:   make TARGET=amiga test-amiga-endian
    exit /b 1
)

echo Assembling %BUNDLE%\ ...

rem --- test binary -----------------------------------------------------------
if not exist "%BUNDLE%\" mkdir "%BUNDLE%"
copy /B /Y "%AMIGA_BIN%" "%BUNDLE%\test_amiga_endian" >nul
echo   copied test_amiga_endian

rem --- TLV files -------------------------------------------------------------
if not exist "%BUNDLE%\output\" mkdir "%BUNDLE%\output"
copy /B /Y "output\Game(2026-04-17).tlv" "%BUNDLE%\output\Game(2026-04-17).tlv" >nul
echo   copied output\Game(2026-04-17).tlv
copy /B /Y "output\Mags(2025-07-24).tlv" "%BUNDLE%\output\Mags(2025-07-24).tlv" >nul
echo   copied output\Mags(2025-07-24).tlv

rem --- CSV definition files --------------------------------------------------
if not exist "%BUNDLE%\assets_raw\defs\" mkdir "%BUNDLE%\assets_raw\defs"
xcopy /Y /I /B "assets_raw\defs\*.*" "%BUNDLE%\assets_raw\defs\" >nul
echo   copied assets_raw\defs\

rem --- profile files ---------------------------------------------------------
if not exist "%BUNDLE%\assets_raw\profiles\" mkdir "%BUNDLE%\assets_raw\profiles"
copy /B /Y "assets_raw\profiles\pal_aga_4mb.profile"           "%BUNDLE%\assets_raw\profiles\" >nul
copy /B /Y "assets_raw\profiles\chipset_legacy_only.profile"   "%BUNDLE%\assets_raw\profiles\" >nul
copy /B /Y "assets_raw\profiles\multi_bucket_reference.profile" "%BUNDLE%\assets_raw\profiles\" >nul
echo   copied assets_raw\profiles\ (3 profiles)

echo.
echo Done.  Bundle is ready at %BUNDLE%\
echo.
echo WinUAE setup:
echo   CD ^& Hard drives ^> Add Directory or Archive
echo   Path : %CD%\%BUNDLE%
echo   Label: ENDIAN    Device: DH1    Read-Write: yes
echo.
echo Amiga CLI:
echo   STACK 100000
echo   cd DH1:
echo   test_amiga_endian ^>output/amiga_results.txt
echo.
echo Results will appear at:
echo   %BUNDLE%\output\amiga_results.txt

endlocal
