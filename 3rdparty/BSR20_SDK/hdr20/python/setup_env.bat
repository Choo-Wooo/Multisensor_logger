@echo off
setlocal

echo ========================================
echo  HDR20 Python Sample - Environment Setup
echo ========================================
echo.

REM ========================================================================
REM Check 64-bit Python
REM ========================================================================
where py >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python launcher ^(py^) not found.
    echo Please install Python 3.x ^(64-bit^) from https://www.python.org
    goto :done
)

py -3-64 --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 64-bit Python 3 not found.
    echo PySide6 requires 64-bit Python. Please install Python 3.x ^(64-bit^).
    goto :done
)

echo [OK] Found Python:
py -3-64 --version
echo.

REM ========================================================================
REM Check required libraries
REM ========================================================================
echo Checking required libraries...
echo.

set MISSING=0

py -3-64 -c "import PySide6" >nul 2>&1
if errorlevel 1 (
    echo   [X] PySide6       - NOT installed
    set MISSING=1
) else (
    echo   [O] PySide6       - OK
)

py -3-64 -c "import pyqtgraph" >nul 2>&1
if errorlevel 1 (
    echo   [X] pyqtgraph     - NOT installed
    set MISSING=1
) else (
    echo   [O] pyqtgraph     - OK
)

py -3-64 -c "import OpenGL" >nul 2>&1
if errorlevel 1 (
    echo   [X] PyOpenGL      - NOT installed
    set MISSING=1
) else (
    echo   [O] PyOpenGL      - OK
)

py -3-64 -c "import numpy" >nul 2>&1
if errorlevel 1 (
    echo   [X] numpy         - NOT installed
    set MISSING=1
) else (
    echo   [O] numpy         - OK
)

echo.

if "%MISSING%"=="0" (
    echo [OK] All libraries are installed!
    echo.
    echo You can run the samples:
    echo   py -3-64 hdr20_radar.py          ^(console^)
    echo   py -3-64 sample_3d_viewer.py     ^(3D viewer^)
    goto :done
)

echo ----------------------------------------
echo Some libraries are missing.
echo.
set /p "INSTALL=Install missing libraries now? (Y/N): "

if /i not "%INSTALL%"=="Y" (
    echo.
    echo Skipped. You can install manually:
    echo   py -3-64 -m pip install -r requirements.txt
    goto :done
)

echo.
echo Installing from requirements.txt...
echo.
py -3-64 -m pip install -r "%~dp0requirements.txt"

if errorlevel 1 (
    echo.
    echo [ERROR] Installation failed.
    goto :done
)

echo.
echo [OK] All libraries installed successfully!
echo.
echo You can now run:
echo   py -3-64 hdr20_radar.py          ^(console^)
echo   py -3-64 sample_3d_viewer.py     ^(3D viewer^)

:done
echo.
pause
