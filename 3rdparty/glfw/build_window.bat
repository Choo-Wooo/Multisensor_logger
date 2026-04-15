@echo off
setlocal enabledelayedexpansion

REM GLFW Windows DLL Build and Deploy Script

echo ========================================
echo GLFW 3.4 DLL Build Script
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
set ZIP_FILE=glfw-3.4.zip
set EXTRACT_DIR=glfw-3.4
set LIB_OUTPUT_DIR=lib\window
set INCLUDE_OUTPUT_DIR=include

REM 1. Check ZIP file
if not exist "%ZIP_FILE%" (
    echo [ERROR] %ZIP_FILE% not found!
    echo Please download GLFW source from https://github.com/glfw/glfw/releases
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
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
echo.

REM 3. Configure CMake for DLL build
echo [3/5] Configuring GLFW build with CMake...
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
        -DBUILD_SHARED_LIBS=ON ^
        -DGLFW_BUILD_EXAMPLES=OFF ^
        -DGLFW_BUILD_TESTS=OFF ^
        -DGLFW_BUILD_DOCS=OFF
    if !ERRORLEVEL! EQU 0 (
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
    )
)

REM Method 2: Try Visual Studio generators
if !GENERATOR_FOUND! EQU 0 (
    echo [INFO] Trying Visual Studio generators...

    REM Try VS 2022
    cmake .. -G "Visual Studio 17 2022" -A x64 ^
        -DBUILD_SHARED_LIBS=ON ^
        -DGLFW_BUILD_EXAMPLES=OFF ^
        -DGLFW_BUILD_TESTS=OFF ^
        -DGLFW_BUILD_DOCS=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 17 2022
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2019
    cmake .. -G "Visual Studio 16 2019" -A x64 ^
        -DBUILD_SHARED_LIBS=ON ^
        -DGLFW_BUILD_EXAMPLES=OFF ^
        -DGLFW_BUILD_TESTS=OFF ^
        -DGLFW_BUILD_DOCS=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 16 2019
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2017
    cmake .. -G "Visual Studio 15 2017 Win64" ^
        -DBUILD_SHARED_LIBS=ON ^
        -DGLFW_BUILD_EXAMPLES=OFF ^
        -DGLFW_BUILD_TESTS=OFF ^
        -DGLFW_BUILD_DOCS=OFF >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Using Visual Studio 15 2017
        set GENERATOR_FOUND=1
        set CONFIG_SUCCESS=1
        goto build_step
    )

    REM Try VS 2015
    cmake .. -G "Visual Studio 14 2015 Win64" ^
        -DBUILD_SHARED_LIBS=ON ^
        -DGLFW_BUILD_EXAMPLES=OFF ^
        -DGLFW_BUILD_TESTS=OFF ^
        -DGLFW_BUILD_DOCS=OFF >nul 2>&1
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
            -DBUILD_SHARED_LIBS=ON ^
            -DGLFW_BUILD_EXAMPLES=OFF ^
            -DGLFW_BUILD_TESTS=OFF ^
            -DGLFW_BUILD_DOCS=OFF
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
echo [4/5] Building GLFW DLL...
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
    if exist "GLFW.sln" (
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

REM Copy DLL and LIB files for Visual Studio build
if exist "src\Release\glfw3.dll" (
    copy /Y "src\Release\glfw3.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3.dll
)
if exist "src\Release\glfw3.lib" (
    copy /Y "src\Release\glfw3.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3.lib
)
if exist "src\Release\glfw3dll.lib" (
    copy /Y "src\Release\glfw3dll.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3dll.lib
)

REM Copy for NMake/MinGW build (src directory)
if exist "src\glfw3.dll" (
    copy /Y "src\glfw3.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3.dll
)
if exist "src\libglfw3.dll" (
    copy /Y "src\libglfw3.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied libglfw3.dll
)
if exist "src\glfw3.lib" (
    copy /Y "src\glfw3.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3.lib
)
if exist "src\glfw3dll.lib" (
    copy /Y "src\glfw3dll.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied glfw3dll.lib
)
if exist "src\libglfw3dll.a" (
    copy /Y "src\libglfw3dll.a" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied libglfw3dll.a
)

cd ..\..

REM Copy header files
echo.
echo Copying header files...
if not exist "%INCLUDE_OUTPUT_DIR%\GLFW" mkdir "%INCLUDE_OUTPUT_DIR%\GLFW"
copy /Y "%EXTRACT_DIR%\include\GLFW\*.h" "%INCLUDE_OUTPUT_DIR%\GLFW\"

echo.
echo ========================================
echo [SUCCESS] GLFW build completed!
echo ========================================
echo.
echo Output files:
echo   DLL: %LIB_OUTPUT_DIR%\glfw3.dll
echo   LIB: %LIB_OUTPUT_DIR%\glfw3.lib (or glfw3dll.lib)
echo   Headers: %INCLUDE_OUTPUT_DIR%\GLFW\*.h
echo.
echo Usage in your project:
echo   1. Link against: glfw3dll.lib (for DLL) or glfw3.lib
echo   2. Include: #include ^<GLFW/glfw3.h^>
echo   3. Make sure glfw3.dll is in your exe directory or PATH
echo.
echo Cleaning up temporary files...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
echo Done!
echo.

pause