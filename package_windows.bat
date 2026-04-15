@echo off
REM Windows x86 배포 패키지 생성
setlocal

set BUILD_DIR=cmake-build-release-visual-studio\bin\Release
set PKG_DIR=dist\MultisensorLogger_win32_v1.0.0

if not exist %BUILD_DIR%\MultisensorLogger.exe (
    echo [ERROR] Build not found. Build Release first.
    exit /b 1
)

echo Creating package in %PKG_DIR%...
if exist %PKG_DIR% rmdir /s /q %PKG_DIR%
mkdir %PKG_DIR%

REM Copy exe and all DLLs
xcopy /y %BUILD_DIR%\*.exe %PKG_DIR%\
xcopy /y %BUILD_DIR%\*.dll %PKG_DIR%\

REM Copy config
copy /y config.ini %PKG_DIR%\

REM Create empty Data folder
mkdir %PKG_DIR%\Data

REM Create README
echo Multi-Sensor Logger > %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo Requirements: Visual C++ Redistributable (x86) >> %PKG_DIR%\README.txt
echo   Download: https://aka.ms/vs/17/release/vc_redist.x86.exe >> %PKG_DIR%\README.txt
echo. >> %PKG_DIR%\README.txt
echo Run: MultisensorLogger.exe >> %PKG_DIR%\README.txt

REM Create zip
powershell Compress-Archive -Path %PKG_DIR% -DestinationPath %PKG_DIR%.zip -Force

echo.
echo Package created: %PKG_DIR%.zip
endlocal
