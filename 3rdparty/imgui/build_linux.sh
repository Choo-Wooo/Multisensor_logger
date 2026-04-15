#!/bin/bash

# ImGui Linux SO Build and Deploy Script (with Backends)

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "========================================"
echo "ImGui Linux SO Build Script"
echo "(with GLFW + OpenGL3 Backends)"
echo "========================================"
echo

# Move to script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Path settings
ZIP_FILE="imgui-master.zip"
EXTRACT_DIR="imgui-master"
LIB_OUTPUT_DIR="lib/linux"
INCLUDE_OUTPUT_DIR="include"

# Check required packages
check_dependencies() {
    echo -e "${BLUE}[0/5] Checking dependencies...${NC}"

    local missing_deps=()

    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake")
    fi

    if ! command -v g++ &> /dev/null; then
        missing_deps+=("g++")
    fi

    if ! command -v unzip &> /dev/null; then
        missing_deps+=("unzip")
    fi

    if [ ${#missing_deps[@]} -ne 0 ]; then
        echo -e "${RED}[ERROR] Missing dependencies: ${missing_deps[*]}${NC}"
        echo "Install with: sudo apt-get install ${missing_deps[*]} build-essential"
        exit 1
    fi

    # Check for OpenGL and GLFW
    if ! pkg-config --exists gl 2>/dev/null; then
        echo -e "${YELLOW}[WARNING] OpenGL development libraries may be missing${NC}"
        echo "Install with: sudo apt-get install libgl1-mesa-dev"
    fi

    if ! pkg-config --exists glfw3 2>/dev/null; then
        echo -e "${YELLOW}[WARNING] GLFW3 development libraries may be missing${NC}"
        echo "You need to build GLFW first or install: sudo apt-get install libglfw3-dev"
    fi

    echo -e "${GREEN}[SUCCESS] All dependencies satisfied${NC}"
    echo
}

# Check dependencies
check_dependencies

# 1. Check ZIP file
if [ ! -f "$ZIP_FILE" ]; then
    echo -e "${RED}[ERROR] $ZIP_FILE not found!${NC}"
    echo "Please download imgui source from https://github.com/ocornut/imgui"
    echo "Or run: curl -L https://github.com/ocornut/imgui/archive/refs/heads/master.zip -o imgui-master.zip"
    exit 1
fi

echo -e "${BLUE}[1/5] Extracting $ZIP_FILE...${NC}"

# Remove old extracted files
if [ -d "$EXTRACT_DIR" ]; then
    echo "Removing old extracted files..."
    rm -rf "$EXTRACT_DIR"
fi

# Extract ZIP
unzip -q "$ZIP_FILE" -d .

if [ ! -d "$EXTRACT_DIR" ]; then
    echo -e "${RED}[ERROR] Failed to extract $ZIP_FILE${NC}"
    exit 1
fi

echo -e "${GREEN}[SUCCESS] Extraction completed${NC}"
echo

# 2. Create output directories
echo -e "${BLUE}[2/5] Creating output directories...${NC}"
mkdir -p "$LIB_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR/backends"
echo

# 3. Generate CMakeLists.txt for SO build with backends
echo -e "${BLUE}[3/5] Generating CMakeLists.txt for SO build (with backends)...${NC}"
cd "$EXTRACT_DIR"

cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.10)
project(ImGui)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set OpenGL preference to GLVND (modern) to avoid warning
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

# Enable Position Independent Code for shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ImGui core source files
set(IMGUI_SOURCES
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_demo.cpp
)

# Backend source files
set(IMGUI_BACKEND_SOURCES
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
)

# Create shared library (SO) with backends
add_library(imgui SHARED 
    ${IMGUI_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

# Include directories
target_include_directories(imgui PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# Find OpenGL (required)
find_package(OpenGL REQUIRED)

# Find GLFW - try local build first, then system installation
set(GLFW_LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../glfw")
set(GLFW_INCLUDE_LOCAL "${GLFW_LOCAL_PATH}/include")
set(GLFW_LIB_LOCAL "${GLFW_LOCAL_PATH}/lib/linux")

message(STATUS "Checking for local GLFW at: ${GLFW_LOCAL_PATH}")
message(STATUS "  Include: ${GLFW_INCLUDE_LOCAL}/GLFW/glfw3.h")
message(STATUS "  Library: ${GLFW_LIB_LOCAL}/libglfw.so")

if(EXISTS "${GLFW_INCLUDE_LOCAL}/GLFW/glfw3.h" AND EXISTS "${GLFW_LIB_LOCAL}/libglfw.so")
    # Use local GLFW build
    message(STATUS "✓ Using local GLFW from: ${GLFW_LOCAL_PATH}")
    target_include_directories(imgui PRIVATE ${GLFW_INCLUDE_LOCAL})
    target_link_directories(imgui PRIVATE ${GLFW_LIB_LOCAL})
    target_link_libraries(imgui PUBLIC glfw)
else()
    # Try pkg-config for system GLFW
    message(STATUS "✗ Local GLFW not found, trying system GLFW...")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW glfw3)
        if(GLFW_FOUND)
            message(STATUS "Using system GLFW via pkg-config")
            target_include_directories(imgui PRIVATE ${GLFW_INCLUDE_DIRS})
            target_link_directories(imgui PRIVATE ${GLFW_LIBRARY_DIRS})
            target_link_libraries(imgui PUBLIC ${GLFW_LIBRARIES})
        else()
            message(FATAL_ERROR "GLFW not found! Please build GLFW first or install: sudo apt-get install libglfw3-dev")
        endif()
    else()
        message(FATAL_ERROR "GLFW not found and pkg-config unavailable!")
    endif()
endif()

# Link OpenGL and other dependencies
target_link_libraries(imgui PUBLIC
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)

# Define OpenGL loader
target_compile_definitions(imgui PRIVATE 
    IMGUI_IMPL_OPENGL_LOADER_GLAD
)

# Set output name and version
set_target_properties(imgui PROPERTIES
    OUTPUT_NAME "imgui"
    VERSION 1.0.0
    SOVERSION 1
)

# Set RPATH for local GLFW (if used)
if(EXISTS "${GLFW_LIB_LOCAL}/libglfw.so")
    set_target_properties(imgui PROPERTIES
        BUILD_RPATH "${GLFW_LIB_LOCAL}"
        INSTALL_RPATH "${GLFW_LIB_LOCAL}"
    )
    message(STATUS "Set RPATH to: ${GLFW_LIB_LOCAL}")
endif()

# Print configuration info
message(STATUS "========================================")
message(STATUS "ImGui Build Configuration:")
message(STATUS "  - Core files: imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp")
message(STATUS "  - Backends: GLFW + OpenGL3")
if(EXISTS "${GLFW_LIB_LOCAL}/libglfw.so")
    message(STATUS "  - GLFW: Local build (${GLFW_LOCAL_PATH})")
else()
    message(STATUS "  - GLFW: System installation")
endif()
message(STATUS "  - OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "========================================")
EOF

echo

# 4. CMake build
echo -e "${BLUE}[4/5] Building ImGui SO with backends...${NC}"
mkdir -p build
cd build

# Set GLFW paths for CMake to find
GLFW_ROOT_DIR="$SCRIPT_DIR/../glfw"
if [ -d "$GLFW_ROOT_DIR/lib/linux" ]; then
    echo "Found local GLFW build at: $GLFW_ROOT_DIR"
    CMAKE_EXTRA_FLAGS="-DGLFW_ROOT=$GLFW_ROOT_DIR"
else
    echo "Local GLFW not found, will try system GLFW..."
    CMAKE_EXTRA_FLAGS=""
fi

cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS=-m32 \
    -DCMAKE_CXX_FLAGS=-m32 \
    $CMAKE_EXTRA_FLAGS

if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] CMake configuration failed!${NC}"
    echo
    echo "Possible solutions:"
    echo "  1. Build GLFW first: cd ../glfw && ./build_glfw_linux.sh"
    echo "  2. Or install system GLFW: sudo apt-get install libglfw3-dev"
    cd ../..
    exit 1
fi

cmake --build . -- -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] Build failed!${NC}"
    cd ../..
    exit 1
fi

echo -e "${GREEN}[SUCCESS] Build completed${NC}"
echo

# 5. Copy files
echo -e "${BLUE}[5/5] Copying files to output directories...${NC}"

# Copy SO files
SO_FOUND=0
if [ -f "libimgui.so" ]; then
    cp -v libimgui.so "../../$LIB_OUTPUT_DIR/"
    echo -e "${GREEN}Copied libimgui.so${NC}"
    SO_FOUND=1
fi

# Copy versioned SO files if they exist
if [ -f "libimgui.so.1" ]; then
    cp -v libimgui.so.1* "../../$LIB_OUTPUT_DIR/" 2>/dev/null
fi

if [ $SO_FOUND -eq 0 ]; then
    echo -e "${YELLOW}[WARNING] libimgui.so not found in build directory${NC}"
    echo "Checking for SO files..."
    ls -la *.so* 2>/dev/null || echo "No SO files found"
fi

cd ../..

# Copy header files
echo
echo "Copying core header files..."
cp -v "$EXTRACT_DIR/imgui.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/imconfig.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/imgui_internal.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/imstb_rectpack.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/imstb_textedit.h" "$INCLUDE_OUTPUT_DIR/"
cp -v "$EXTRACT_DIR/imstb_truetype.h" "$INCLUDE_OUTPUT_DIR/"

echo "Copying backend header files..."
cp -v "$EXTRACT_DIR/backends/imgui_impl_glfw.h" "$INCLUDE_OUTPUT_DIR/backends/"
cp -v "$EXTRACT_DIR/backends/imgui_impl_opengl3.h" "$INCLUDE_OUTPUT_DIR/backends/"
cp -v "$EXTRACT_DIR/backends/imgui_impl_opengl3_loader.h" "$INCLUDE_OUTPUT_DIR/backends/"

echo
echo "========================================"
echo -e "${GREEN}[SUCCESS] Build completed successfully!${NC}"
echo "========================================"
echo
echo "Output files:"
echo "  SO:  $LIB_OUTPUT_DIR/libimgui.so"
echo "  Headers: $INCLUDE_OUTPUT_DIR/*.h"
echo "           $INCLUDE_OUTPUT_DIR/backends/*.h"
echo
echo "Included backends:"
echo "  - ImGui_ImplGlfw_* (GLFW linux support)"
echo "  - ImGui_ImplOpenGL3_* (OpenGL3 rendering)"
echo
echo "Usage in your project:"
echo "  #include <imgui.h>"
echo "  #include <backends/imgui_impl_glfw.h>"
echo "  #include <backends/imgui_impl_opengl3.h>"
echo

# Clean up temporary files
echo "Cleaning up temporary files..."
if [ -d "$EXTRACT_DIR" ]; then
    rm -rf "$EXTRACT_DIR"
fi
echo -e "${GREEN}Done!${NC}"
echo