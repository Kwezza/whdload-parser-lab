@echo off
rem assemble_tlv_build_bundle.bat
rem
rem Assembles a self-contained Amiga test bundle in amiga_tlv_bundle\.
rem The Amiga dat_to_tlv binary reads the bundled DAT files and writes
rem five TLV files to amiga_tlv_bundle\output\.
rem
rem Mount amiga_tlv_bundle\ in WinUAE as a read-write directory hard drive.
rem
rem From the Amiga CLI:
rem   STACK 100000
rem   cd DH1:
rem   dat_to_tlv
rem
rem After the run, validate on the PC with:
rem   fc /b "output\DemB(2026-04-20).tlv"  "amiga_tlv_bundle\output\DemB(2026-04-20).tlv"
rem   fc /b "output\Demo(2026-03-23).tlv"  "amiga_tlv_bundle\output\Demo(2026-03-23).tlv"
rem   fc /b "output\GamB(2026-04-26).tlv"  "amiga_tlv_bundle\output\GamB(2026-04-26).tlv"
rem   fc /b "output\Game(2026-04-17).tlv"  "amiga_tlv_bundle\output\Game(2026-04-17).tlv"
rem   fc /b "output\Mags(2025-07-24).tlv"  "amiga_tlv_bundle\output\Mags(2025-07-24).tlv"
rem
rem All copies use xcopy /B (binary mode) to preserve CRLF line endings in the
rem CSV files.  The CRC validator expects CRLF; do not convert line endings.
rem
rem See docs\tests\amiga-tlv-creation-test-plan.md for the full test plan.
rem
rem Requires: build\amiga\dat_to_tlv must exist.
rem Run:      make TARGET=amiga   (to build the binary)
rem

setlocal

set BUNDLE=amiga_tlv_bundle
set AMIGA_BIN=build\amiga\dat_to_tlv

if not exist "%AMIGA_BIN%" (
    echo ERROR: %AMIGA_BIN% not found.
    echo Run:   make TARGET=amiga
    exit /b 1
)

echo Assembling %BUNDLE%\ ...

rem --- Amiga binary ----------------------------------------------------------
if not exist "%BUNDLE%\" mkdir "%BUNDLE%"
copy /B /Y "%AMIGA_BIN%" "%BUNDLE%\dat_to_tlv" >nul
echo   copied dat_to_tlv

rem --- DAT input files -------------------------------------------------------
if not exist "%BUNDLE%\assets_raw\Dats\" mkdir "%BUNDLE%\assets_raw\Dats"
xcopy /Y /I /B "assets_raw\Dats\*.*" "%BUNDLE%\assets_raw\Dats\" >nul
echo   copied assets_raw\Dats\ (5 DAT files)

rem --- CSV definition files --------------------------------------------------
if not exist "%BUNDLE%\assets_raw\defs\" mkdir "%BUNDLE%\assets_raw\defs"
xcopy /Y /I /B "assets_raw\defs\*.*" "%BUNDLE%\assets_raw\defs\" >nul
echo   copied assets_raw\defs\

rem --- prefs -----------------------------------------------------------------
if not exist "%BUNDLE%\assets_raw\prefs\" mkdir "%BUNDLE%\assets_raw\prefs"
copy /B /Y "assets_raw\prefs\pack_types.ini" "%BUNDLE%\assets_raw\prefs\pack_types.ini" >nul
echo   copied assets_raw\prefs\pack_types.ini

rem --- output directory (empty, dat_to_tlv writes here) ----------------------
if not exist "%BUNDLE%\output\" mkdir "%BUNDLE%\output"
echo   created output\ (empty, dat_to_tlv writes TLV files here)

echo.
echo Done.  Bundle is ready at %BUNDLE%\
echo.
echo WinUAE setup:
echo   CD ^& Hard drives ^> Add Directory or Archive
echo   Path : %CD%\%BUNDLE%
echo   Label: TLVBLD   Device: DH1    Read-Write: yes
echo.
echo Amiga CLI:
echo   STACK 100000
echo   cd DH1:
echo   dat_to_tlv
echo.
echo Expected output (0 errors for all 5 pack types):
echo   DemB: 12 variants processed, 0 errors
echo   Demo: 904 variants processed, 0 errors
echo   GamB: 128 variants processed, 0 errors
echo   Game: 3973 variants processed, 0 errors
echo   Mags: 104 variants processed, 0 errors
echo.
echo After the run, validate on the PC (each should report no differences):
echo   fc /b "output\DemB(2026-04-20).tlv"  "%BUNDLE%\output\DemB(2026-04-20).tlv"
echo   fc /b "output\Demo(2026-03-23).tlv"  "%BUNDLE%\output\Demo(2026-03-23).tlv"
echo   fc /b "output\GamB(2026-04-26).tlv"  "%BUNDLE%\output\GamB(2026-04-26).tlv"
echo   fc /b "output\Game(2026-04-17).tlv"  "%BUNDLE%\output\Game(2026-04-17).tlv"
echo   fc /b "output\Mags(2025-07-24).tlv"  "%BUNDLE%\output\Mags(2025-07-24).tlv"

endlocal
