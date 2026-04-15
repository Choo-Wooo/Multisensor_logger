@echo off
cd /d "%~dp0"

echo ========================================
echo  BSR20 SDK Sample - Windows Build
echo ========================================
echo.

echo [1/2] Configuring with CMake...
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Please check:
    echo   1. Run this script from "Developer Command Prompt for VS"
    echo   2. CMake is installed and in PATH
    echo   3. window\sdk\  directory exists
    echo.
    pause
    exit /b 1
)
echo [OK] Configuration successful
echo.

echo [2/2] Building...
cmake --build build
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Build completed successfully!
echo ========================================
echo.
echo Output:
echo   build\BSR20_Sample.exe
echo   build\BSR20_SDK.dll
echo.
echo Run:
echo   build\BSR20_Sample.exe [radar_ip] [port]
echo   build\BSR20_Sample.exe 192.168.172.128 7
echo.
pause
