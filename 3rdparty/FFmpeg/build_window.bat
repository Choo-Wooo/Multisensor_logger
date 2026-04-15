@echo off
setlocal enabledelayedexpansion

echo ========================================
echo FFmpeg 8.0.1 Windows x86 DLL Build Script
echo (live555 RTSP stream decoding - minimal build)
echo ========================================
echo.

REM ---------------------------------------------------------
REM Variables
REM ---------------------------------------------------------
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

set SOURCE_FILE=ffmpeg-8.0.1.tar.xz
set EXTRACT_DIR=ffmpeg-8.0.1
set LIB_OUTPUT_DIR=lib\window
set INCLUDE_OUTPUT_DIR=include

set CORES=%NUMBER_OF_PROCESSORS%
if not defined CORES set CORES=4

REM ---------------------------------------------------------
REM [0/5] Dependency check
REM ---------------------------------------------------------
echo [0/5] Checking dependencies...
echo.

REM --- MSYS2 ---
set MSYS2_ROOT=
for %%P in (
    "C:\msys64"
    "C:\msys2"
    "%SystemDrive%\msys64"
    "%SystemDrive%\msys2"
    "%USERPROFILE%\scoop\apps\msys2\current"
) do (
    if exist "%%~P\usr\bin\bash.exe" (
        set MSYS2_ROOT=%%~P
        goto :found_msys2
    )
)

echo [ERROR] MSYS2 not found!
echo.
echo Install MSYS2 from: https://www.msys2.org/
echo Then run in MSYS2 terminal:
echo   pacman -S make nasm mingw-w64-i686-gcc mingw-w64-i686-tools
echo.
pause
exit /b 1

:found_msys2
echo [OK] MSYS2: !MSYS2_ROOT!
set MSYS2_BASH=!MSYS2_ROOT!\usr\bin\bash.exe

REM --- make ---
if not exist "!MSYS2_ROOT!\usr\bin\make.exe" (
    echo.
    echo [ERROR] 'make' not found in MSYS2!
    echo Run in MSYS2 terminal: pacman -S make
    echo.
    pause
    exit /b 1
)
echo [OK] make: !MSYS2_ROOT!\usr\bin\make.exe

REM --- MinGW-w64 i686 gcc (REQUIRED - x86 target, MSVC toolchain not supported by FFmpeg 8.x) ---
if not exist "!MSYS2_ROOT!\mingw32\bin\gcc.exe" (
    echo.
    echo [ERROR] MinGW-w64 i686 gcc not found!
    echo.
    echo FFmpeg 8.x MSVC toolchain is not supported - deprecated linker flags.
    echo MinGW-w64 i686 is required for Windows x86 builds.
    echo.
    echo Run in MSYS2 terminal:
    echo   pacman -S mingw-w64-i686-gcc mingw-w64-i686-tools
    echo.
    pause
    exit /b 1
)
echo [OK] MinGW-w64 i686 gcc: !MSYS2_ROOT!\mingw32\bin\gcc.exe

REM --- MSVC lib.exe (optional - for MSVC-compatible .lib generation) ---
set USE_MSVC=0

where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] MSVC: already in PATH - will use lib.exe for .lib generation
    set USE_MSVC=1
    goto :check_nasm
)

set VSWHERE=
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if defined VSWHERE (
    for /f "usebackq delims=" %%I in (`%VSWHERE% -latest -property installationPath 2^>nul`) do (
        if exist "%%I\VC\Auxiliary\Build\vcvarsall.bat" (
            set VCVARSALL=%%I\VC\Auxiliary\Build\vcvarsall.bat
        )
    )
)

if not defined VCVARSALL (
    for %%P in (
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    ) do (
        if exist "%%~P" (
            set VCVARSALL=%%~P
            goto :vcvarsall_found
        )
    )
)

:vcvarsall_found
if defined VCVARSALL (
    call "!VCVARSALL!" x86 >nul 2>&1
    set USE_MSVC=1
    echo [OK] MSVC: !VCVARSALL! - will use lib.exe for .lib generation
) else (
    echo [INFO] MSVC not found - will use dlltool for .lib generation
)

:check_nasm
REM --- NASM (optional - enables assembly optimizations) ---
set HAS_NASM=0

where nasm >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] NASM: found in PATH
    set HAS_NASM=1
    goto :deps_done
)

if exist "!MSYS2_ROOT!\mingw32\bin\nasm.exe" (
    echo [OK] NASM: !MSYS2_ROOT!\mingw32\bin\nasm.exe
    set HAS_NASM=1
    goto :deps_done
)

echo [INFO] NASM not found - assembly optimizations disabled
echo        Install: pacman -S nasm

:deps_done
echo.

REM ---------------------------------------------------------
REM [1/5] Extract source
REM ---------------------------------------------------------
echo [1/5] Extracting %SOURCE_FILE%...

if not exist "%SOURCE_FILE%" (
    echo [ERROR] %SOURCE_FILE% not found in %SCRIPT_DIR%
    pause
    exit /b 1
)

if exist "%EXTRACT_DIR%" (
    echo [INFO] Removing old extraction: %EXTRACT_DIR%
    rmdir /s /q "%EXTRACT_DIR%"
)

echo [INFO] Trying Windows built-in tar...
tar -xf "%SOURCE_FILE%"
if %ERRORLEVEL% EQU 0 goto :extract_ok

echo [INFO] Windows tar failed - trying MSYS2 tar...
call :win_to_msys2_path "%CD%" MSYS2_FALLBACK_DIR
"!MSYS2_BASH!" --noprofile --norc -c "cd '!MSYS2_FALLBACK_DIR!' && tar -xf '%SOURCE_FILE%'"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Extraction failed! Check that %SOURCE_FILE% is not corrupted.
    echo.
    pause
    exit /b 1
)

:extract_ok
if not exist "%EXTRACT_DIR%" (
    echo [ERROR] Extraction directory not found: %EXTRACT_DIR%
    pause
    exit /b 1
)
echo [OK] Extraction completed: %EXTRACT_DIR%
echo.

REM ---------------------------------------------------------
REM [2/5] Create output directories
REM ---------------------------------------------------------
echo [2/5] Creating output directories...
if not exist "lib" mkdir "lib"
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
echo [OK] Directories ready
echo.

REM ---------------------------------------------------------
REM [3/5] Configure FFmpeg (MinGW-w64 toolchain)
REM
REM Note: FFmpeg 8.x MSVC toolchain is broken (deprecated -o linker flag).
REM       MinGW-w64 is used for compilation. MSVC lib.exe is used only
REM       for .lib generation in step [5/5].
REM
REM Enabled:
REM   libavcodec  - H.264 / H.265 / MJPEG / MPEG4 decoder
REM   libavutil   - required utility library for avcodec
REM   libswscale  - YUV->RGB/RGBA conversion (for OpenGL texture)
REM
REM Disabled:
REM   libavformat   - demuxing (live555 handles this)
REM   libavdevice   - capture devices (not needed)
REM   libavfilter   - filters (not needed)
REM   libswresample - audio resampling (not needed)
REM   network       - networking (live555 handles this)
REM ---------------------------------------------------------
echo [3/5] Configuring FFmpeg (MinGW-w64)...
echo.

call :win_to_msys2_path "%CD%" MSYS2_SRC_DIR
set MSYS2_FFMPEG_DIR=!MSYS2_SRC_DIR!/!EXTRACT_DIR!
echo [INFO] MSYS2 source dir: !MSYS2_FFMPEG_DIR!

set CFG=./configure
set CFG=!CFG! --prefix=./ffmpeg_output
set CFG=!CFG! --enable-shared
set CFG=!CFG! --disable-static
set CFG=!CFG! --disable-programs
set CFG=!CFG! --disable-doc
set CFG=!CFG! --disable-avdevice
set CFG=!CFG! --disable-avformat
set CFG=!CFG! --disable-swresample
set CFG=!CFG! --disable-avfilter
set CFG=!CFG! --disable-network
set CFG=!CFG! --disable-everything
set CFG=!CFG! --enable-avcodec
set CFG=!CFG! --enable-avutil
set CFG=!CFG! --enable-swscale
set CFG=!CFG! --enable-decoder=h264
set CFG=!CFG! --enable-decoder=hevc
set CFG=!CFG! --enable-decoder=mpeg4
set CFG=!CFG! --enable-decoder=mjpeg
set CFG=!CFG! --enable-decoder=rawvideo
set CFG=!CFG! --enable-parser=h264
set CFG=!CFG! --enable-parser=hevc
set CFG=!CFG! --enable-parser=mpeg4video
set CFG=!CFG! --enable-bsf=h264_mp4toannexb
set CFG=!CFG! --enable-bsf=hevc_mp4toannexb
set CFG=!CFG! --arch=x86
set CFG=!CFG! --target-os=mingw32

if !HAS_NASM! EQU 0 (
    set CFG=!CFG! --disable-x86asm
    echo [INFO] x86 ASM: disabled - install nasm for better performance
) else (
    echo [INFO] x86 ASM: enabled
)
echo [INFO] Toolchain: MinGW-w64 i686 x86
echo [INFO] Running configure...
echo.

REM Write configure script to a file to avoid batch quoting issues.
REM PATH must include /mingw32/bin for gcc and /usr/bin for make/bash tools.
set CFG_SH=%EXTRACT_DIR%\ffbuild_configure.sh
echo #!/bin/bash                                               > "!CFG_SH!"
echo set -e                                                   >> "!CFG_SH!"
echo export PATH="/mingw32/bin:/usr/bin:/usr/local/bin:$PATH" >> "!CFG_SH!"
echo cd '!MSYS2_FFMPEG_DIR!'                                 >> "!CFG_SH!"
echo !CFG!                                                    >> "!CFG_SH!"

call :win_to_msys2_path "%CD%\!CFG_SH!" MSYS2_CFG_SH

"!MSYS2_BASH!" --noprofile --norc "!MSYS2_CFG_SH!"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Configure failed!
    echo.
    echo Tip: Check %EXTRACT_DIR%\ffbuild\config.log for details.
    echo.
    pause
    exit /b 1
)
echo.
echo [OK] Configure completed
echo.

REM ---------------------------------------------------------
REM [4/5] Build
REM ---------------------------------------------------------
echo [4/5] Building FFmpeg with %CORES% cores (this may take several minutes)...
echo.

set MAKE_SH=%EXTRACT_DIR%\ffbuild_make.sh
echo #!/bin/bash                                               > "!MAKE_SH!"
echo set -e                                                   >> "!MAKE_SH!"
echo export PATH="/mingw32/bin:/usr/bin:/usr/local/bin:$PATH" >> "!MAKE_SH!"
echo cd '!MSYS2_FFMPEG_DIR!'                                 >> "!MAKE_SH!"
echo make -j!CORES!                                          >> "!MAKE_SH!"
echo make install                                            >> "!MAKE_SH!"

call :win_to_msys2_path "%CD%\!MAKE_SH!" MSYS2_MAKE_SH

"!MSYS2_BASH!" --noprofile --norc "!MSYS2_MAKE_SH!"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)
echo.
echo [OK] Build completed
echo.

REM ---------------------------------------------------------
REM [5/5] Copy output files
REM ---------------------------------------------------------
echo [5/5] Copying output files...
echo.

set BUILD_OUTPUT=%EXTRACT_DIR%\ffmpeg_output

REM Copy DLLs
echo [INFO] Copying DLLs to %LIB_OUTPUT_DIR%...
for %%F in ("%BUILD_OUTPUT%\bin\avcodec*.dll") do (
    copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    echo   Copied: %%~nxF
)
for %%F in ("%BUILD_OUTPUT%\bin\avutil*.dll") do (
    copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    echo   Copied: %%~nxF
)
for %%F in ("%BUILD_OUTPUT%\bin\swscale*.dll") do (
    copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    echo   Copied: %%~nxF
)

REM Generate MSVC-compatible .lib import libraries
echo.
echo [INFO] Generating MSVC-compatible .lib files...

call :win_to_msys2_path "%SCRIPT_DIR%%LIB_OUTPUT_DIR%" MSYS2_LIB_DIR

REM Step 1: Use gendef to extract export definitions from DLLs
"!MSYS2_BASH!" --noprofile --norc -c "export PATH='/mingw32/bin:$PATH' && cd '!MSYS2_LIB_DIR!' && gendef avcodec*.dll avutil*.dll swscale*.dll 2>&1"

if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] gendef failed - install mingw-w64-i686-tools
    echo           pacman -S mingw-w64-i686-tools
    echo [INFO] Falling back to .dll.a import libraries - MinGW format...
    for %%F in ("%BUILD_OUTPUT%\lib\libavcodec*.dll.a") do copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    for %%F in ("%BUILD_OUTPUT%\lib\libavutil*.dll.a") do copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    for %%F in ("%BUILD_OUTPUT%\lib\libswscale*.dll.a") do copy /Y "%%F" "%LIB_OUTPUT_DIR%\" >nul
    goto :copy_headers
)

REM Step 1.5: Strip @N stdcall decorations from .def exports for MSVC cdecl compatibility
REM   gendef on x86 MinGW DLLs writes e.g. "av_packet_free@0" (stdcall decoration).
REM   MSVC x86 cdecl code calls "_av_packet_free" (underscore prefix, no @N).
REM   lib.exe /machine:x86 on a plain "av_packet_free" entry creates "_av_packet_free".
REM   Strip the @N suffix so lib.exe generates correct cdecl import symbols.
echo [INFO] Stripping @N suffixes from .def exports (MSVC cdecl compatibility)...
"!MSYS2_BASH!" --noprofile --norc -c "export PATH='/mingw32/bin:/usr/bin:$PATH' && cd '!MSYS2_LIB_DIR!' && sed -i 's/@[0-9][0-9]*$//' avcodec-*.def avutil-*.def swscale-*.def && echo '  Done.'"
if %ERRORLEVEL% NEQ 0 (
    echo [WARNING] sed strip step failed - .def files may retain @N suffixes causing MSVC linker errors
    echo           Linker will report LNK2019 for free-type functions e.g. av_packet_free
)

REM Step 2: Generate .lib from .def files
if !USE_MSVC! EQU 1 (
    echo [INFO] Using MSVC lib.exe for .lib generation...
    for %%D in ("%LIB_OUTPUT_DIR%\avcodec-*.def") do (
        lib /def:"%%~fD" /out:"%LIB_OUTPUT_DIR%\avcodec.lib" /machine:x86 >nul 2>&1
        echo   Generated: avcodec.lib
    )
    for %%D in ("%LIB_OUTPUT_DIR%\avutil-*.def") do (
        lib /def:"%%~fD" /out:"%LIB_OUTPUT_DIR%\avutil.lib" /machine:x86 >nul 2>&1
        echo   Generated: avutil.lib
    )
    for %%D in ("%LIB_OUTPUT_DIR%\swscale-*.def") do (
        lib /def:"%%~fD" /out:"%LIB_OUTPUT_DIR%\swscale.lib" /machine:x86 >nul 2>&1
        echo   Generated: swscale.lib
    )
) else (
    echo [INFO] Using dlltool for .lib generation...
    "!MSYS2_BASH!" --noprofile --norc -c "export PATH='/mingw32/bin:$PATH' && cd '!MSYS2_LIB_DIR!' && for def in avcodec-*.def avutil-*.def swscale-*.def; do base=${def%%-*}; dlltool -d $def -l ${base}.lib && echo \"  Generated: ${base}.lib\"; done"
)

:copy_headers
echo.
echo [INFO] Copying headers to %INCLUDE_OUTPUT_DIR%...
if not exist "%INCLUDE_OUTPUT_DIR%\libavcodec" mkdir "%INCLUDE_OUTPUT_DIR%\libavcodec"
if not exist "%INCLUDE_OUTPUT_DIR%\libavutil"  mkdir "%INCLUDE_OUTPUT_DIR%\libavutil"
if not exist "%INCLUDE_OUTPUT_DIR%\libswscale" mkdir "%INCLUDE_OUTPUT_DIR%\libswscale"

xcopy /Y /E "%BUILD_OUTPUT%\include\libavcodec\*" "%INCLUDE_OUTPUT_DIR%\libavcodec\" >nul
xcopy /Y /E "%BUILD_OUTPUT%\include\libavutil\*"  "%INCLUDE_OUTPUT_DIR%\libavutil\"  >nul
xcopy /Y /E "%BUILD_OUTPUT%\include\libswscale\*" "%INCLUDE_OUTPUT_DIR%\libswscale\" >nul
echo   Copied: libavcodec/ libavutil/ libswscale/

echo.
echo [INFO] Cleaning up extracted source...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"

echo.
echo ========================================
echo [SUCCESS] FFmpeg 8.0.1 build completed!
echo ========================================
echo.
echo Output:
echo   %LIB_OUTPUT_DIR%\   -- DLL + .lib files
echo   %INCLUDE_OUTPUT_DIR%\   -- header files
echo.
echo Enabled decoders : h264, hevc, mpeg4, mjpeg, rawvideo
echo Enabled parsers  : h264, hevc, mpeg4video
echo Enabled BSF      : h264_mp4toannexb, hevc_mp4toannexb
echo.

pause
goto :eof

REM ---------------------------------------------------------
REM Subroutine: Convert Windows path to MSYS2 Unix path
REM   Usage: call :win_to_msys2_path "C:\some\path" RESULT_VAR
REM   Result: /c/some/path
REM ---------------------------------------------------------
:win_to_msys2_path
setlocal
set WIN_P=%~1
set WIN_P=!WIN_P:\=/!
set WIN_P=!WIN_P: =%20!
set DRV=!WIN_P:~0,1!
set REST=!WIN_P:~2!
for %%L in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do (
    if /i "!DRV!"=="%%L" set DRV=%%L
)
endlocal & set %~2=/%DRV%%REST%
goto :eof
