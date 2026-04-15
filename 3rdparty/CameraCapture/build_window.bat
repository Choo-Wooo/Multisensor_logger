@echo off
setlocal enabledelayedexpansion

REM CameraCapture Windows DLL Build and Deploy Script

echo ========================================
echo CameraCapture 1.7.0 DLL Build Script
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
set ZIP_FILE=CameraCapture-1.7.0.zip
set EXTRACT_DIR=CameraCapture-1.7.0
set LIB_OUTPUT_DIR=lib\window\async
set INCLUDE_OUTPUT_DIR=include

REM 1. Check zip file
if not exist "%ZIP_FILE%" (
    echo [ERROR] %ZIP_FILE% not found!
    echo Please place the CameraCapture source zip in this directory.
    pause
    exit /b 1
)

echo [1/5] Extracting %ZIP_FILE%...
REM Remove old extracted files
if exist "%EXTRACT_DIR%" (
    echo Removing old extracted files...
    rmdir /s /q "%EXTRACT_DIR%"
)

REM Extract using PowerShell
echo Extracting zip file...
powershell -Command "Expand-Archive -Path '%ZIP_FILE%' -DestinationPath '.' -Force"

if not exist "%EXTRACT_DIR%" (
    echo [ERROR] Failed to extract %ZIP_FILE%
    pause
    exit /b 1
)

echo [SUCCESS] Extraction completed
echo.

REM 2. Create output directories
echo [2/5] Creating output directories...
if not exist "lib" mkdir "lib"
if not exist "lib\window" mkdir "lib\window"
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
echo Created: %LIB_OUTPUT_DIR%
echo Created: %INCLUDE_OUTPUT_DIR%
echo.

REM 3. Configure CMake
echo [3/5] Configuring CameraCapture build with CMake...
cd "%EXTRACT_DIR%"

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
        -DCMAKE_BUILD_TYPE=Release ^
        -DCCAP_BUILD_SHARED=ON ^
        -DCCAP_BUILD_TESTS=OFF ^
        -DCCAP_BUILD_EXAMPLES=OFF ^
        -DCCAP_BUILD_CLI=OFF ^
        -DCCAP_INSTALL=OFF
    if !ERRORLEVEL! EQU 0 (
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
    )
)

REM Method 2: Try Visual Studio generators
if !GENERATOR_FOUND! EQU 0 (
    echo [INFO] Trying Visual Studio generators...

    REM Try VS 2022
    cmake .. -G "Visual Studio 17 2022" -A Win32 ^
        -DCCAP_BUILD_SHARED=ON ^
        -DCCAP_BUILD_TESTS=OFF ^
        -DCCAP_BUILD_EXAMPLES=OFF ^
        -DCCAP_BUILD_CLI=OFF ^
        -DCCAP_INSTALL=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 17 2022
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2019
    cmake .. -G "Visual Studio 16 2019" -A Win32 ^
        -DCCAP_BUILD_SHARED=ON ^
        -DCCAP_BUILD_TESTS=OFF ^
        -DCCAP_BUILD_EXAMPLES=OFF ^
        -DCCAP_BUILD_CLI=OFF ^
        -DCCAP_INSTALL=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 16 2019
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2017
    cmake .. -G "Visual Studio 15 2017" -A Win32 ^
        -DCCAP_BUILD_SHARED=ON ^
        -DCCAP_BUILD_TESTS=OFF ^
        -DCCAP_BUILD_EXAMPLES=OFF ^
        -DCCAP_BUILD_CLI=OFF ^
        -DCCAP_INSTALL=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 15 2017
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2015
    cmake .. -G "Visual Studio 14 2015" -A Win32 ^
        -DCCAP_BUILD_SHARED=ON ^
        -DCCAP_BUILD_TESTS=OFF ^
        -DCCAP_BUILD_EXAMPLES=OFF ^
        -DCCAP_BUILD_CLI=OFF ^
        -DCCAP_INSTALL=OFF >nul 2>&1
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
            -DCMAKE_BUILD_TYPE=Release ^
            -DCCAP_BUILD_SHARED=ON ^
            -DCCAP_BUILD_TESTS=OFF ^
            -DCCAP_BUILD_EXAMPLES=OFF ^
            -DCCAP_BUILD_CLI=OFF ^
            -DCCAP_INSTALL=OFF
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
    cd ..\..
    pause
    exit /b 1
)

echo [SUCCESS] CMake configuration completed
echo.

:build_step
REM 4. Build
echo [4/5] Building CameraCapture DLL...
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
    if exist "ccap.sln" (
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
    cd ..\..
    pause
    exit /b 1
)

echo [SUCCESS] Build completed
echo.

REM 5. Copy files
echo [5/5] Copying files to output directories...

REM Copy DLL and import LIB for Visual Studio build (Release subfolder)
if exist "Release\ccap.dll" (
    copy /Y "Release\ccap.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied ccap.dll to %LIB_OUTPUT_DIR%
)
if exist "Release\ccap.lib" (
    copy /Y "Release\ccap.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied ccap.lib to %LIB_OUTPUT_DIR%
)

REM Copy for NMake/MinGW build (root build directory)
if exist "ccap.dll" (
    copy /Y "ccap.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied ccap.dll to %LIB_OUTPUT_DIR%
)
if exist "ccap.lib" (
    copy /Y "ccap.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied ccap.lib to %LIB_OUTPUT_DIR%
)
if exist "libccap.dll" (
    copy /Y "libccap.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied libccap.dll to %LIB_OUTPUT_DIR%
)
if exist "libccap.dll.a" (
    copy /Y "libccap.dll.a" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied libccap.dll.a to %LIB_OUTPUT_DIR%
)

cd ..\..

REM Copy header files
echo.
echo Copying header files...
copy /Y "%EXTRACT_DIR%\include\*.h" "%INCLUDE_OUTPUT_DIR%\"
echo Copied headers to %INCLUDE_OUTPUT_DIR%

echo.
echo ========================================
echo [SUCCESS] CameraCapture build completed!
echo ========================================
echo.
echo Output files:
echo   Library: %LIB_OUTPUT_DIR%\ccap.dll
echo            %LIB_OUTPUT_DIR%\ccap.lib  (import library)
echo   Headers: %INCLUDE_OUTPUT_DIR%\ccap.h
echo            %INCLUDE_OUTPUT_DIR%\ccap_*.h
echo.
echo Usage in your project:
echo   1. Link against: %LIB_OUTPUT_DIR%\ccap.lib (import lib for DLL)
echo   2. Include: #include ^<ccap.h^>
echo   3. Make sure ccap.dll is in your exe directory or PATH
echo.
echo Note: ccap requires Windows Media Foundation (mf, mfplat, mfreadwrite, mfuuid)
echo       These are system DLLs available on Windows 7 and later.
echo.
echo Cleaning up temporary files...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
echo Done!
echo.

pause