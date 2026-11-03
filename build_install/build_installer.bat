@echo off
REM ===========================================================================
REM Multi-Sensor Logger - Windows installer builder
REM ===========================================================================
REM This script lives in <project>/build_install/ and operates on the parent
REM project directory. It will:
REM   1. Configure + build the project in Release config (Visual Studio)
REM   2. Verify the resulting exe + DLLs are in place
REM   3. Run NSIS (makensis) to compile installer.nsi -> setup .exe
REM
REM Requirements (one-time setup):
REM   - Visual Studio 2022 with C++ workload     (any edition)
REM   - CMake 3.16+ on PATH
REM   - NSIS 3.x installed                       https://nsis.sourceforge.io
REM       Either add it to PATH, or install to default location:
REM         C:\Program Files (x86)\NSIS\makensis.exe
REM         C:\Program Files\NSIS\makensis.exe
REM
REM Optional:
REM   Drop "VC_redist.x64.exe" next to installer.nsi (in this directory) and
REM   the installer will offer to install the VC++ runtime automatically.
REM ===========================================================================

setlocal EnableDelayedExpansion

REM SCRIPT_DIR = build_install/, PROJECT_ROOT = parent
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%.."
set PROJECT_ROOT=%CD%
popd

echo Project root  : %PROJECT_ROOT%
echo Script dir    : %SCRIPT_DIR%
echo.

REM ---------- Configurable knobs ----------------------------------------------
set BUILD_DIR=%PROJECT_ROOT%\cmake-build-release-visual-studio
set BUILD_CONFIG=Release
set GENERATOR=Visual Studio 17 2022
set ARCH=x64
set TARGET=MultisensorLogger

set STAGE_DIR=%BUILD_DIR%\bin\%BUILD_CONFIG%

REM ---------- Step 1: configure -----------------------------------------------
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [1/3] Configuring CMake project...
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH%
    if errorlevel 1 goto :fail
) else (
    echo [1/3] CMake cache exists, skipping configure step.
)

REM ---------- Step 2: build ---------------------------------------------------
echo.
echo [2/3] Building %TARGET% (%BUILD_CONFIG%)...
cmake --build "%BUILD_DIR%" --target %TARGET% --config %BUILD_CONFIG% -- /m
if errorlevel 1 goto :fail

if not exist "%STAGE_DIR%\MultisensorLogger.exe" (
    echo [ERROR] Build succeeded but MultisensorLogger.exe missing in:
    echo         %STAGE_DIR%
    goto :fail
)

REM ---------- Step 3: locate makensis -----------------------------------------
echo.
echo [3/3] Compiling installer with NSIS...

REM Resolve makensis path. Avoid parenthesised if-blocks here because
REM "%ProgramFiles(x86)%" contains literal parens that confuse cmd's parser
REM even when quoted. Use plain GOTO chains instead.
set "PF_X86=%ProgramFiles(x86)%"
set "PF_X64=%ProgramFiles%"

set MAKENSIS=
where makensis >nul 2>&1
if not errorlevel 1 set MAKENSIS=makensis
if defined MAKENSIS goto :have_makensis

if not exist "%PF_X86%\NSIS\makensis.exe" goto :try_pfx64
set "MAKENSIS=%PF_X86%\NSIS\makensis.exe"
goto :have_makensis

:try_pfx64
if not exist "%PF_X64%\NSIS\makensis.exe" goto :no_makensis
set "MAKENSIS=%PF_X64%\NSIS\makensis.exe"
goto :have_makensis

:no_makensis
echo [ERROR] makensis.exe not found.
echo         Install NSIS from https://nsis.sourceforge.io/
echo         and either add it to PATH or install to the default location.
goto :fail

:have_makensis
echo Using makensis: %MAKENSIS%

REM Pull version out of installer.nsi for the output filename.
REM NOTE: don't include the leading "!" in the search string - delayed
REM expansion (EnableDelayedExpansion) eats it and the match silently fails.
for /f "tokens=3 delims= " %%V in ('findstr /C:"define APP_VERSION" "%SCRIPT_DIR%installer.nsi"') do set APP_VERSION=%%V
set APP_VERSION=%APP_VERSION:"=%

set OUTPUT_NAME=MultisensorLogger_Setup_v%APP_VERSION%.exe

REM Run makensis from the script dir so File paths in installer.nsi resolve
REM relative to build_install/. Use absolute STAGE_DIR.
cd /d "%SCRIPT_DIR%"
"%MAKENSIS%" /V2 /DSTAGE_DIR="%STAGE_DIR%" /DPROJECT_ROOT="%PROJECT_ROOT%" /DOUTPUT_NAME="%OUTPUT_NAME%" installer.nsi
if errorlevel 1 goto :fail

if not exist "%SCRIPT_DIR%%OUTPUT_NAME%" goto :fail_missing

echo.
echo ===========================================================================
echo  Installer built: %SCRIPT_DIR%%OUTPUT_NAME%
echo ===========================================================================
endlocal
exit /b 0

:fail_missing
echo [ERROR] NSIS reported success but %OUTPUT_NAME% not produced.

:fail
echo.
echo *** Build failed. ***
endlocal
exit /b 1
