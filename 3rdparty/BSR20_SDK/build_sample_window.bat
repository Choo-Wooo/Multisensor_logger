@echo off
setlocal enabledelayedexpansion

echo ====================================
echo  BSR20 SDK Samples - Windows Build
echo ====================================
echo.
echo Targets:
echo   - bsr20_sample      (console)
echo   - hdr20_sample      (console)
echo   - bsr20_vds_viewer  (GUI, ImGui+OpenGL)
echo.

REM ========================================================================
REM Select platform (x86 / x64)
REM ========================================================================
echo Select build platform:
echo   1. x86 (32-bit)
echo   2. x64 (64-bit)
echo.
set /p "PLATFORM_CHOICE=Enter choice (1 or 2): "

if "%PLATFORM_CHOICE%"=="2" (
    set "BUILD_ARCH=x64"
    set "CMAKE_ARCH=-A x64"
) else (
    set "BUILD_ARCH=x86"
    set "CMAKE_ARCH=-A Win32"
)
echo.
echo [INFO] Selected platform: %BUILD_ARCH%
echo.

REM ========================================================================
REM Setup MSVC environment (auto-detect Visual Studio)
REM ========================================================================
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] MSVC not in PATH, searching for Visual Studio...

    set "VCVARS="

    for %%e in (Community Professional Enterprise BuildTools) do (
        if exist "C:\Program Files\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvarsall.bat"
            goto :found_vs
        )
    )

    for %%e in (Community Professional Enterprise BuildTools) do (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\%%e\VC\Auxiliary\Build\vcvarsall.bat"
            goto :found_vs
        )
    )

    echo [ERROR] Visual Studio not found!
    echo Please install Visual Studio with C++ workload.
    pause
    exit /b 1

    :found_vs
    echo [INFO] Found: !VCVARS!
    call "!VCVARS!" %BUILD_ARCH% >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Failed to initialize MSVC environment
        pause
        exit /b 1
    )
    echo [OK] MSVC environment ready
) else (
    echo [OK] MSVC already in PATH
)
echo.

REM ========================================================================
REM Build using Visual Studio generator (supports FetchContent for VDS Viewer)
REM ========================================================================
if exist build (
    rmdir /S /Q build
)
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" %CMAKE_ARCH% -DCMAKE_SYSTEM_VERSION=10.0
if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    pause
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

echo.
echo [OK] Build complete! (%BUILD_ARCH%)
echo.
echo Output:
echo   build\Release\bsr20_sample.exe
echo   build\Release\hdr20_sample.exe
echo   build\bsr20\cpp_vds_viewer\Release\bsr20_vds_viewer.exe
pause
