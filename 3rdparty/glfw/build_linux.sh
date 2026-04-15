#!/bin/bash

# GLFW Linux Shared Library Build and Deploy Script
sudo apt-get install cmake build-essential unzip
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
sudo apt-get install libwayland-dev wayland-protocols libxkbcommon-dev

set -e  # Exit on error

echo "========================================"
echo "GLFW 3.4 Shared Library Build Script"
echo "(Ubuntu/Linux Build)"
echo "========================================"
echo ""

# Save current script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check dependencies
echo "[0/5] Checking dependencies..."

# Check CMake
if ! command -v cmake &> /dev/null; then
    echo ""
    echo "[ERROR] CMake is not installed!"
    echo ""
    echo "Please install CMake with one of these commands:"
    echo "  Ubuntu/Debian: sudo apt-get install cmake"
    echo "  Fedora:        sudo dnf install cmake"
    echo "  Arch:          sudo pacman -S cmake"
    echo ""
    echo "After installation, run this script again."
    echo ""
    exit 1
fi

echo "[SUCCESS] CMake found: $(cmake --version | head -n1)"

# Check build tools
if ! command -v make &> /dev/null; then
    echo ""
    echo "[ERROR] Make is not installed!"
    echo ""
    echo "Please install build tools:"
    echo "  Ubuntu/Debian: sudo apt-get install build-essential"
    echo "  Fedora:        sudo dnf groupinstall 'Development Tools'"
    echo "  Arch:          sudo pacman -S base-devel"
    echo ""
    exit 1
fi

echo "[SUCCESS] Make found"

# Check required development packages
echo ""
echo "Checking X11 development libraries..."
if ! pkg-config --exists x11 xrandr xinerama xcursor xi 2>/dev/null; then
    echo ""
    echo "[WARNING] Some X11 development libraries may be missing."
    echo ""
    echo "Please install required packages:"
    echo "  Ubuntu/Debian:"
    echo "    sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev"
    echo "    sudo apt-get install libxcursor-dev libxi-dev libxext-dev"
    echo ""
    echo "  Fedora:"
    echo "    sudo dnf install libX11-devel libXrandr-devel libXinerama-devel"
    echo "    sudo dnf install libXcursor-devel libXi-devel libXext-devel"
    echo ""
    echo "Continuing anyway... build may fail without these."
    echo ""
else
    echo "[SUCCESS] X11 development libraries found"
fi

echo ""

# Path settings
ZIP_FILE="glfw-3.4.zip"
EXTRACT_DIR="glfw-3.4"
LIB_OUTPUT_DIR="lib/linux"
INCLUDE_OUTPUT_DIR="include"

# 1. Check ZIP file
if [ ! -f "$ZIP_FILE" ]; then
    echo "[ERROR] $ZIP_FILE not found!"
    echo "Please download GLFW source from https://github.com/glfw/glfw/releases"
    exit 1
fi

echo "[1/5] Extracting $ZIP_FILE..."

# Remove old extracted files
if [ -d "$EXTRACT_DIR" ]; then
    echo "Removing old extracted files..."
    rm -rf "$EXTRACT_DIR"
fi

# Check for unzip command
if ! command -v unzip &> /dev/null; then
    echo "[ERROR] unzip is not installed!"
    echo "Install with: sudo apt-get install unzip"
    exit 1
fi

# Extract
unzip -q "$ZIP_FILE"

if [ ! -d "$EXTRACT_DIR" ]; then
    echo "[ERROR] Failed to extract $ZIP_FILE"
    exit 1
fi

echo "[SUCCESS] Extraction completed"
echo ""

# 2. Create output directories
echo "[2/5] Creating output directories..."
mkdir -p "$LIB_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR"
echo ""

# 3. Configure CMake for shared library build
echo "[3/5] Configuring GLFW build with CMake..."
cd "$EXTRACT_DIR"

mkdir -p build
cd build

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DGLFW_BUILD_EXAMPLES=OFF \
    -DGLFW_BUILD_TESTS=OFF \
    -DGLFW_BUILD_DOCS=OFF \
    -DCMAKE_C_FLAGS=-m32 \
    -DCMAKE_CXX_FLAGS=-m32

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] CMake configuration failed!"
    echo ""
    echo "Please make sure you have all required development packages installed."
    echo "See the warnings above for package installation commands."
    echo ""
    cd ../..
    exit 1
fi

echo "[SUCCESS] CMake configuration completed"
echo ""

# 4. Build
echo "[4/5] Building GLFW shared library..."

# Get number of CPU cores for parallel build
NPROC=$(nproc 2>/dev/null || echo 4)
echo "Building with $NPROC parallel jobs..."

make -j$NPROC

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] Build failed!"
    echo ""
    cd ../..
    exit 1
fi

echo "[SUCCESS] Build completed"
echo ""

# 5. Copy files
echo "[5/5] Copying files to output directories..."

# Copy shared library files
if [ -f "src/libglfw.so.3.4" ]; then
    cp -v "src/libglfw.so.3.4" "../../$LIB_OUTPUT_DIR/"
    echo "Copied libglfw.so.3.4"
fi

if [ -f "src/libglfw.so.3" ]; then
    cp -v "src/libglfw.so.3" "../../$LIB_OUTPUT_DIR/"
    echo "Copied libglfw.so.3"
fi

if [ -f "src/libglfw.so" ]; then
    cp -v "src/libglfw.so" "../../$LIB_OUTPUT_DIR/"
    echo "Copied libglfw.so"
fi

# Create symlinks if main .so file exists
cd "../../$LIB_OUTPUT_DIR"
if [ -f "libglfw.so.3.4" ]; then
    ln -sf libglfw.so.3.4 libglfw.so.3
    ln -sf libglfw.so.3 libglfw.so
    echo "Created symlinks: libglfw.so.3 -> libglfw.so.3.4"
    echo "                  libglfw.so -> libglfw.so.3"
fi
cd "$SCRIPT_DIR"

cd "$EXTRACT_DIR/build"

# Copy pkg-config file if exists
if [ -f "src/glfw3.pc" ]; then
    cp -v "src/glfw3.pc" "../../$LIB_OUTPUT_DIR/"
    echo "Copied glfw3.pc"
fi

cd ../..

# Copy header files
echo ""
echo "Copying header files..."
mkdir -p "$INCLUDE_OUTPUT_DIR/GLFW"
cp -v "$EXTRACT_DIR/include/GLFW/"*.h "$INCLUDE_OUTPUT_DIR/GLFW/"

echo ""
echo "========================================"
echo "[SUCCESS] GLFW build completed!"
echo "========================================"
echo ""
echo "Output files:"
echo "  Library: $LIB_OUTPUT_DIR/libglfw.so.3.4"
echo "           $LIB_OUTPUT_DIR/libglfw.so.3 (symlink)"
echo "           $LIB_OUTPUT_DIR/libglfw.so (symlink)"
echo "  Headers: $INCLUDE_OUTPUT_DIR/GLFW/*.h"
echo ""
echo "Usage in your project:"
echo "  1. Compile: gcc -c your_code.c -I$INCLUDE_OUTPUT_DIR"
echo "  2. Link: gcc your_code.o -L$LIB_OUTPUT_DIR -lglfw -lGL -lm -ldl -lpthread"
echo "  3. Include: #include <GLFW/glfw3.h>"
echo ""
echo "Runtime library path:"
echo "  Add to LD_LIBRARY_PATH: export LD_LIBRARY_PATH=\$PWD/$LIB_OUTPUT_DIR:\$LD_LIBRARY_PATH"
echo "  Or install to system: sudo cp $LIB_OUTPUT_DIR/libglfw.so* /usr/local/lib/"
echo "                        sudo ldconfig"
echo ""
echo "Cleaning up temporary files..."
rm -rf "$EXTRACT_DIR"
echo "Done!"
echo ""