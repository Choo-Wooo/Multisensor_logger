@echo off
setlocal enabledelayedexpansion

REM live555 Windows Static Library Build and Deploy Script

echo ========================================
echo live555 2026.01.12 Static Library Build Script
echo (Windows Library Build)
echo ========================================
echo.

REM Save current script location
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM Check CMake installation
echo [0/5] Checking dependencies...
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake is not installed or not in PATH!
    echo.
    echo Please install CMake from one of these options:
    echo   1. Download from: https://cmake.org/download/
    echo   2. Install with winget: winget install Kitware.CMake
    echo   3. Install with chocolatey: choco install cmake
    echo.
    echo After installation, restart your terminal and run this script again.
    echo.
    pause
    exit /b 1
)

echo [SUCCESS] CMake found
echo.

REM Path settings
set TAR_GZ_FILE=live.2026.01.12.tar.gz
set TAR_FILE=live.2026.01.12.tar
set EXTRACT_DIR=live
set LIB_OUTPUT_DIR=lib\window
set INCLUDE_OUTPUT_DIR=include

REM 1. Check tar.gz file
if not exist "%TAR_GZ_FILE%" (
    echo [ERROR] %TAR_GZ_FILE% not found!
    echo Please download live555 source from http://www.live555.com/liveMedia/
    pause
    exit /b 1
)

echo [1/5] Extracting %TAR_GZ_FILE%...
REM Remove old extracted files
if exist "%EXTRACT_DIR%" (
    echo Removing old extracted files...
    rmdir /s /q "%EXTRACT_DIR%"
)
if exist "%TAR_FILE%" (
    del /q "%TAR_FILE%"
)

REM Extract using PowerShell (supports tar.gz)
echo Decompressing .tar.gz file...
powershell -Command "& {$inFile='%TAR_GZ_FILE%'; $outFile='%TAR_FILE%'; $input=New-Object System.IO.FileStream $inFile,([IO.FileMode]::Open),([IO.FileAccess]::Read),([IO.FileShare]::Read); $output=New-Object System.IO.FileStream $outFile,([IO.FileMode]::Create),([IO.FileAccess]::Write),([IO.FileShare]::None); $gzipStream=New-Object System.IO.Compression.GzipStream $input,([IO.Compression.CompressionMode]::Decompress); $buffer=New-Object byte[](1024); while(($read=$gzipStream.Read($buffer,0,1024)) -gt 0){$output.Write($buffer,0,$read)}; $gzipStream.Close(); $output.Close(); $input.Close()}"

if not exist "%TAR_FILE%" (
    echo [ERROR] Failed to decompress %TAR_GZ_FILE%
    pause
    exit /b 1
)

echo Extracting .tar file...
powershell -Command "tar -xf '%TAR_FILE%'"

if not exist "%EXTRACT_DIR%" (
    echo [ERROR] Failed to extract %TAR_FILE%
    pause
    exit /b 1
)

REM Clean up tar file
del /q "%TAR_FILE%"

echo [SUCCESS] Extraction completed
echo.

REM 2. Create output directories
echo [2/5] Creating output directories...
if not exist "lib" mkdir "lib"
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
echo Created: %LIB_OUTPUT_DIR%
echo Created: %INCLUDE_OUTPUT_DIR%
echo.

REM 3. Configure CMake
echo [3/5] Configuring live555 build with CMake...

if not exist build mkdir build
cd build

REM Detect available compiler/generator
set GENERATOR_FOUND=0
set CONFIG_SUCCESS=0

REM Method 1: Use NMake if Visual Studio is in PATH
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected Visual Studio compiler, using NMake Makefiles...
    cmake .. -G "NMake Makefiles" ^
        -DCMAKE_BUILD_TYPE=Release
    if !ERRORLEVEL! EQU 0 (
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
    )
)

REM Method 2: Try Visual Studio generators
if !GENERATOR_FOUND! EQU 0 (
    echo [INFO] Trying Visual Studio generators...

    REM Try VS 2022
    cmake .. -G "Visual Studio 17 2022" -A x64 >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 17 2022
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2019
    cmake .. -G "Visual Studio 16 2019" -A x64 >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 16 2019
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2017
    cmake .. -G "Visual Studio 15 2017 Win64" >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 15 2017
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2015
    cmake .. -G "Visual Studio 14 2015 Win64" >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 14 2015
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )
)

REM Method 3: Try MinGW
if !GENERATOR_FOUND! EQU 0 (
    where mingw32-make >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Detected MinGW, using MinGW Makefiles...
        cmake .. -G "MinGW Makefiles" ^
            -DCMAKE_BUILD_TYPE=Release
        if !ERRORLEVEL! EQU 0 (
            set GENERATOR_FOUND=1
            set CONFIG_SUCCESS=1
        )
    )
)

REM Handle configuration failure
if !CONFIG_SUCCESS! EQU 0 (
    echo.
    echo [ERROR] CMake configuration failed or no suitable build system found!
    echo.
    echo Please install one of the following:
    echo   1. Visual Studio Build Tools
    echo      Download: https://visualstudio.microsoft.com/downloads/
    echo      Install "Desktop development with C++" workload
    echo.
    echo   2. MinGW-w64
    echo      Download: https://www.mingw-w64.org/downloads/
    echo      Or install via: winget install mingw
    echo.
    echo   3. Run this script from "Developer Command Prompt for VS"
    echo      Start Menu -^> Visual Studio -^> Developer Command Prompt
    echo.
    cd ..
    pause
    exit /b 1
)

echo [SUCCESS] CMake configuration completed
echo.

:build_step
REM 4. Build
echo [4/5] Building live555 static libraries...
set BUILD_SUCCESS=0

REM Check if NMake
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    where nmake >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        nmake
        if !ERRORLEVEL! EQU 0 (
            set BUILD_SUCCESS=1
        )
    )
)

REM Check if Visual Studio generator
if !BUILD_SUCCESS! EQU 0 (
    if exist "live555.sln" (
        cmake --build . --config Release
        if !ERRORLEVEL! EQU 0 (
            set BUILD_SUCCESS=1
        )
    )
)

REM Check if MinGW
if !BUILD_SUCCESS! EQU 0 (
    where mingw32-make >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        mingw32-make
        if !ERRORLEVEL! EQU 0 (
            set BUILD_SUCCESS=1
        )
    )
)

if !BUILD_SUCCESS! EQU 0 (
    echo.
    echo [ERROR] Build failed!
    echo.
    cd ..
    pause
    exit /b 1
)

echo [SUCCESS] Build completed
echo.

REM 5. Copy files
echo [5/5] Copying files to output directories...

REM Copy .lib files for Visual Studio build (Release subfolder)
for %%L in (liveMedia groupsock BasicUsageEnvironment UsageEnvironment) do (
    if exist "Release\%%L.lib" (
        copy /Y "Release\%%L.lib" "..\%LIB_OUTPUT_DIR%\"
        echo Copied %%L.lib to %LIB_OUTPUT_DIR%
    )
)

REM Copy .lib files for NMake build (root build directory)
for %%L in (liveMedia groupsock BasicUsageEnvironment UsageEnvironment) do (
    if exist "%%L.lib" (
        copy /Y "%%L.lib" "..\%LIB_OUTPUT_DIR%\"
        echo Copied %%L.lib to %LIB_OUTPUT_DIR%
    )
)

REM Copy .a files for MinGW build
for %%L in (libliveMedia libgroupsock libBasicUsageEnvironment libUsageEnvironment) do (
    if exist "%%L.a" (
        copy /Y "%%L.a" "..\%LIB_OUTPUT_DIR%\"
        echo Copied %%L.a to %LIB_OUTPUT_DIR%
    )
)

cd ..

REM Copy header files
echo.
echo Copying header files...
if not exist "%INCLUDE_OUTPUT_DIR%\liveMedia" mkdir "%INCLUDE_OUTPUT_DIR%\liveMedia"
if not exist "%INCLUDE_OUTPUT_DIR%\groupsock" mkdir "%INCLUDE_OUTPUT_DIR%\groupsock"
if not exist "%INCLUDE_OUTPUT_DIR%\BasicUsageEnvironment" mkdir "%INCLUDE_OUTPUT_DIR%\BasicUsageEnvironment"
if not exist "%INCLUDE_OUTPUT_DIR%\UsageEnvironment" mkdir "%INCLUDE_OUTPUT_DIR%\UsageEnvironment"

copy /Y "%EXTRACT_DIR%\liveMedia\include\*.hh" "%INCLUDE_OUTPUT_DIR%\liveMedia\"
copy /Y "%EXTRACT_DIR%\groupsock\include\*.hh" "%INCLUDE_OUTPUT_DIR%\groupsock\"
copy /Y "%EXTRACT_DIR%\groupsock\include\*.h" "%INCLUDE_OUTPUT_DIR%\groupsock\"
copy /Y "%EXTRACT_DIR%\BasicUsageEnvironment\include\*.hh" "%INCLUDE_OUTPUT_DIR%\BasicUsageEnvironment\"
copy /Y "%EXTRACT_DIR%\UsageEnvironment\include\*.hh" "%INCLUDE_OUTPUT_DIR%\UsageEnvironment\"

echo.
echo ========================================
echo [SUCCESS] live555 build completed!
echo ========================================
echo.
echo Output libraries: %LIB_OUTPUT_DIR%\
echo   - liveMedia.lib
echo   - groupsock.lib
echo   - BasicUsageEnvironment.lib
echo   - UsageEnvironment.lib
echo.
echo Output headers: %INCLUDE_OUTPUT_DIR%\
echo   - liveMedia\*.hh
echo   - groupsock\*.hh, *.h
echo   - BasicUsageEnvironment\*.hh
echo   - UsageEnvironment\*.hh
echo.
echo Usage in your project:
echo   1. Link against the .lib files
echo   2. Add include directories for each module
echo   3. Link ws2_32.lib for Windows socket support
echo.
echo Cleaning up temporary files...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
if exist "build" rmdir /s /q "build"
echo Done!
echo.

pause
