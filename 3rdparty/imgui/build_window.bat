@echo off
setlocal enabledelayedexpansion

REM ImGui Windows DLL Build and Deploy Script (Full Version with Backends including GLFW)

echo ========================================
echo ImGui Full DLL Build Script
echo (Core + Windows + GLFW Backends)
echo ========================================
echo.

REM Save current script location
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM Check CMake installation
echo [0/6] Checking dependencies...
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

REM Check GLFW library
set GLFW_LIB_DIR=lib\window
set GLFW_INCLUDE_DIR=include

if not exist "%GLFW_LIB_DIR%\glfw3.dll" (
    echo.
    echo [WARNING] GLFW library not found in %GLFW_LIB_DIR%
    echo Please build GLFW first using the GLFW build script.
    echo.
    pause
    exit /b 1
)

if not exist "%GLFW_INCLUDE_DIR%\GLFW\glfw3.h" (
    echo.
    echo [WARNING] GLFW headers not found in %GLFW_INCLUDE_DIR%\GLFW
    echo Please build GLFW first using the GLFW build script.
    echo.
    pause
    exit /b 1
)

echo [SUCCESS] GLFW library found
echo.

REM Path settings
set ZIP_FILE=imgui-master.zip
set EXTRACT_DIR=imgui-master
set LIB_OUTPUT_DIR=lib\window
set INCLUDE_OUTPUT_DIR=include

REM 1. Check ZIP file
if not exist "%ZIP_FILE%" (
    echo [ERROR] %ZIP_FILE% not found!
    echo Please download imgui source from https://github.com/ocornut/imgui
    pause
    exit /b 1
)

echo [1/6] Extracting %ZIP_FILE%...
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
echo [2/6] Creating output directories...
if not exist "%LIB_OUTPUT_DIR%" mkdir "%LIB_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%" mkdir "%INCLUDE_OUTPUT_DIR%"
if not exist "%INCLUDE_OUTPUT_DIR%\backends" mkdir "%INCLUDE_OUTPUT_DIR%\backends"
echo.

REM 3. Generate CMakeLists.txt for DLL build with ALL components including GLFW
echo [3/6] Generating CMakeLists.txt with native backends + GLFW...
cd "%EXTRACT_DIR%"

(
echo cmake_minimum_required^(VERSION 3.10^)
echo project^(ImGui^)
echo.
echo set^(CMAKE_CXX_STANDARD 11^)
echo set^(CMAKE_CXX_STANDARD_REQUIRED ON^)
echo.
echo # Enable automatic DLL export for Windows
echo set^(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON^)
echo.
echo # GLFW library paths
echo set^(GLFW_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/window"^)
echo set^(GLFW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include"^)
echo.
echo # ImGui core source files
echo set^(IMGUI_CORE_SOURCES
echo     imgui.cpp
echo     imgui_draw.cpp
echo     imgui_tables.cpp
echo     imgui_widgets.cpp
echo     imgui_demo.cpp
echo ^)
echo.
echo # ImGui backend source files ^(Windows native + GLFW^)
echo set^(IMGUI_BACKEND_SOURCES
echo     backends/imgui_impl_win32.cpp
echo     backends/imgui_impl_dx9.cpp
echo     backends/imgui_impl_dx10.cpp
echo     backends/imgui_impl_dx11.cpp
echo     backends/imgui_impl_dx12.cpp
echo     backends/imgui_impl_opengl2.cpp
echo     backends/imgui_impl_opengl3.cpp
echo     backends/imgui_impl_glfw.cpp
echo ^)
echo.
echo # Optional backends ^(require external SDKs - commented out^)
echo # Uncomment only if you have these SDKs installed:
echo #     backends/imgui_impl_vulkan.cpp    # Requires: Vulkan SDK
echo #     backends/imgui_impl_sdl2.cpp      # Requires: SDL2 library
echo #     backends/imgui_impl_sdl3.cpp      # Requires: SDL3 library
echo #     backends/imgui_impl_glut.cpp      # Requires: GLUT library
echo.
echo # Combine all sources
echo set^(IMGUI_ALL_SOURCES ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES}^)
echo.
echo # Create shared library DLL
echo add_library^(imgui SHARED ${IMGUI_ALL_SOURCES}^)
echo.
echo # Include directories
echo target_include_directories^(imgui PUBLIC
echo     ${CMAKE_CURRENT_SOURCE_DIR}
echo     ${CMAKE_CURRENT_SOURCE_DIR}/backends
echo     ${GLFW_INCLUDE_DIR}
echo ^)
echo.
echo # Find GLFW library
echo find_library^(GLFW_LIBRARY
echo     NAMES glfw3dll glfw3 libglfw3dll
echo     PATHS ${GLFW_LIB_DIR}
echo     NO_DEFAULT_PATH
echo ^)
echo.
echo if^(NOT GLFW_LIBRARY^)
echo     message^(FATAL_ERROR "GLFW library not found in ${GLFW_LIB_DIR}"^)
echo endif^(^)
echo.
echo message^(STATUS "Found GLFW: ${GLFW_LIBRARY}"^)
echo.
echo # Set output name
echo set_target_properties^(imgui PROPERTIES OUTPUT_NAME "imgui"^)
echo.
echo # Windows-specific libraries + GLFW
echo if^(WIN32^)
echo     target_link_libraries^(imgui
echo         d3d9 d3d11 d3d12 dxgi d3dcompiler
echo         opengl32 gdi32 user32 imm32
echo         ${GLFW_LIBRARY}
echo     ^)
echo endif^(^)
) > CMakeLists.txt

echo.

REM 4. CMake build
echo [4/6] Building ImGui DLL with Windows native backends + GLFW...
if not exist build mkdir build
cd build

REM Detect available compiler/generator
set GENERATOR_FOUND=0
set BUILD_SUCCESS=0

REM Method 1: Use NMake if Visual Studio is in PATH
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected Visual Studio compiler, using NMake Makefiles...
    cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    if !ERRORLEVEL! EQU 0 (
        set GENERATOR_FOUND=1
        nmake
        if !ERRORLEVEL! EQU 0 (
            set BUILD_SUCCESS=1
        )
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
        cmake --build . --config Release
        if !ERRORLEVEL! EQU 0 (
            set BUILD_SUCCESS=1
        )
    )

    REM Try VS 2019
    if !GENERATOR_FOUND! EQU 0 (
        cmake .. -G "Visual Studio 16 2019" -A x64 >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            echo [INFO] Using Visual Studio 16 2019
            set GENERATOR_FOUND=1
            cmake --build . --config Release
            if !ERRORLEVEL! EQU 0 (
                set BUILD_SUCCESS=1
            )
        )
    )

    REM Try VS 2017
    if !GENERATOR_FOUND! EQU 0 (
        cmake .. -G "Visual Studio 15 2017 Win64" >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            echo [INFO] Using Visual Studio 15 2017
            set GENERATOR_FOUND=1
            cmake --build . --config Release
            if !ERRORLEVEL! EQU 0 (
                set BUILD_SUCCESS=1
            )
        )
    )

    REM Try VS 2015
    if !GENERATOR_FOUND! EQU 0 (
        cmake .. -G "Visual Studio 14 2015 Win64" >nul 2>&1
        if !ERRORLEVEL! EQU 0 (
            echo [INFO] Using Visual Studio 14 2015
            set GENERATOR_FOUND=1
            cmake --build . --config Release
            if !ERRORLEVEL! EQU 0 (
                set BUILD_SUCCESS=1
            )
        )
    )
)

REM Method 3: Try MinGW
if !GENERATOR_FOUND! EQU 0 (
    where mingw32-make >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo [INFO] Detected MinGW, using MinGW Makefiles...
        cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
        if !ERRORLEVEL! EQU 0 (
            set GENERATOR_FOUND=1
            mingw32-make
            if !ERRORLEVEL! EQU 0 (
                set BUILD_SUCCESS=1
            )
        )
    )
)

REM Handle build failure
if !BUILD_SUCCESS! EQU 0 (
    echo.
    echo [ERROR] Build failed or no suitable build system found!
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

echo [SUCCESS] Build completed
echo.

REM 5. Copy files
echo [5/6] Copying files to output directories...

REM Copy DLL and LIB files for Visual Studio build
if exist "Release\imgui.dll" (
    copy /Y "Release\imgui.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied imgui.dll
)
if exist "Release\imgui.lib" (
    copy /Y "Release\imgui.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied imgui.lib
)

REM Copy for NMake/MinGW build (current directory)
if exist "imgui.dll" (
    copy /Y "imgui.dll" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied imgui.dll
)
if exist "imgui.lib" (
    copy /Y "imgui.lib" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied imgui.lib
)
if exist "libimgui.dll.a" (
    copy /Y "libimgui.dll.a" "..\..\%LIB_OUTPUT_DIR%\"
    echo Copied libimgui.dll.a
)

cd ..\..

REM Copy core header files
echo.
echo Copying core header files...
copy /Y "%EXTRACT_DIR%\imgui.h" "%INCLUDE_OUTPUT_DIR%\"
copy /Y "%EXTRACT_DIR%\imconfig.h" "%INCLUDE_OUTPUT_DIR%\"
copy /Y "%EXTRACT_DIR%\imgui_internal.h" "%INCLUDE_OUTPUT_DIR%\"
copy /Y "%EXTRACT_DIR%\imstb_rectpack.h" "%INCLUDE_OUTPUT_DIR%\"
copy /Y "%EXTRACT_DIR%\imstb_textedit.h" "%INCLUDE_OUTPUT_DIR%\"
copy /Y "%EXTRACT_DIR%\imstb_truetype.h" "%INCLUDE_OUTPUT_DIR%\"

REM Copy backend header files
echo Copying backend header files...
copy /Y "%EXTRACT_DIR%\backends\*.h" "%INCLUDE_OUTPUT_DIR%\backends\"

REM 6. Copy GLFW DLL to output directory (for convenience)
echo.
echo [6/6] Copying GLFW DLL...
if exist "%GLFW_LIB_DIR%\glfw3.dll" (
    copy /Y "%GLFW_LIB_DIR%\glfw3.dll" "%LIB_OUTPUT_DIR%\"
    echo Copied glfw3.dll
)

echo.
echo ========================================
echo [SUCCESS] Full build completed!
echo ========================================
echo.
echo Output files:
echo   DLL: %LIB_OUTPUT_DIR%\imgui.dll
echo   DLL: %LIB_OUTPUT_DIR%\glfw3.dll
echo   LIB: %LIB_OUTPUT_DIR%\imgui.lib
echo   Core Headers: %INCLUDE_OUTPUT_DIR%\*.h
echo   Backend Headers: %INCLUDE_OUTPUT_DIR%\backends\*.h
echo.
echo Included backends:
echo   - Win32
echo   - DirectX 9/10/11/12
echo   - OpenGL 2/3
echo   - GLFW (for window creation)
echo.
echo Usage example (GLFW + OpenGL3):
echo   #include ^<imgui.h^>
echo   #include ^<backends/imgui_impl_glfw.h^>
echo   #include ^<backends/imgui_impl_opengl3.h^>
echo.
echo   Link with: imgui.lib glfw3dll.lib opengl32.lib
echo   Runtime DLLs needed: imgui.dll glfw3.dll
echo.
echo Optional backends (require external SDKs):
echo   - Vulkan (requires Vulkan SDK)
echo   - SDL2/SDL3 (requires SDL library)
echo   - GLUT (requires GLUT library)
echo.
echo Cleaning up temporary files...
if exist "%EXTRACT_DIR%" rmdir /s /q "%EXTRACT_DIR%"
echo Done!
echo.

pause