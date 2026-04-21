@echo off
setlocal enabledelayedexpansion

REM ========================================
echo OpenSSL 3.5.6 Windows x64 DLL Build Script
echo (MSVC x64, DLL, lib\window + include)
echo ========================================
echo.

REM 현재 스크립트 경로
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM 의존성 확인
echo [0/5] Checking dependencies...
where perl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Perl not found! Install Strawberry Perl: https://strawberryperl.com/
    pause
    exit /b 1
)

where nmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] NMake not found! Run this script from 'x64 Native Tools Command Prompt for VS'
    pause
    exit /b 1
)

echo [SUCCESS] Dependencies found
echo.

REM 소스 및 출력 경로
set ZIP_FILE=openssl-openssl-3.5.6.zip
set EXTRACT_DIR=openssl-openssl-3.5.6
set BUILD_DIR=%EXTRACT_DIR%\build
set LIB_OUTPUT_DIR=lib\window
set INCLUDE_OUTPUT_DIR=include

REM 1. ZIP 체크 및 추출
if not exist "%ZIP_FILE%" (
    echo [ERROR] %ZIP_FILE% not found!
    pause
    exit /b 1
)

echo [1/5] Extracting %ZIP_FILE%...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
powershell -Command "Expand-Archive -Path '%ZIP_FILE%' -DestinationPath '.' -Force"
if not exist "%EXTRACT_DIR%" (
    echo [ERROR] Failed to extract %ZIP_FILE%
    pause
    exit /b 1
)
echo [SUCCESS] Extraction done
echo.

REM 2. 출력 폴더 생성
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
echo [2/5] Output directories ready
echo.

REM 3. OpenSSL Configure + NMake Build
cd "%EXTRACT_DIR%"

echo [3/5] Configuring OpenSSL for x86 DLL...
REM shared = DLL, VC-WIN32 = x86
perl Configure VC-WIN32 shared --prefix="%CD%\build"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Configure failed
    pause
    exit /b 1
)

echo Building OpenSSL (this may take several minutes)...
nmake
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

nmake install
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Install failed
    pause
    exit /b 1
)

echo [SUCCESS] OpenSSL build completed
echo.

REM 4. DLL / LIB / Headers 복사
echo [4/5] Copying DLL/LIB to %LIB_OUTPUT_DIR%...
copy /Y "%BUILD_DIR%\bin\*.dll" "..\..\%LIB_OUTPUT_DIR%\" >nul
copy /Y "%BUILD_DIR%\lib\*.lib" "..\..\%LIB_OUTPUT_DIR%\" >nul
echo [SUCCESS] DLL/LIB copied
echo.

echo [5/5] Copying headers to %INCLUDE_OUTPUT_DIR%...
xcopy /Y /E "%BUILD_DIR%\include\*" "..\..\%INCLUDE_OUTPUT_DIR%\" >nul
echo [SUCCESS] Headers copied
echo.

REM 6. Cleanup
cd ..
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
echo Done!
echo.
echo ========================================
echo [SUCCESS] OpenSSL 3.5.6 x64 DLL build completed!
echo ========================================
pause
