#!/bin/bash

# CameraCapture Linux Shared Library Build and Deploy Script

set -e  # Exit on error

echo "========================================"
echo "CameraCapture 1.7.0 Shared Library Build Script"
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

# Check GCC
if ! command -v gcc &> /dev/null; then
    echo ""
    echo "[ERROR] GCC is not installed!"
    echo ""
    echo "Please install GCC:"
    echo "  Ubuntu/Debian: sudo apt-get install build-essential"
    echo "  Fedora:        sudo dnf install gcc"
    echo "  Arch:          sudo pacman -S gcc"
    echo ""
    exit 1
fi

echo "[SUCCESS] GCC found: $(gcc --version | head -n1)"

# Check unzip
if ! command -v unzip &> /dev/null; then
    echo ""
    echo "[ERROR] unzip is not installed!"
    echo ""
    echo "Please install unzip:"
    echo "  Ubuntu/Debian: sudo apt-get install unzip"
    echo "  Fedora:        sudo dnf install unzip"
    echo "  Arch:          sudo pacman -S unzip"
    echo ""
    exit 1
fi

echo "[SUCCESS] unzip found"
echo ""

# Path settings
ZIP_FILE="CameraCapture-1.7.0.zip"
EXTRACT_DIR="CameraCapture-1.7.0"
LIB_OUTPUT_DIR="lib/linux/async"
INCLUDE_OUTPUT_DIR="include"

# 1. Check zip file
if [ ! -f "$ZIP_FILE" ]; then
    echo "[ERROR] $ZIP_FILE not found!"
    echo "Please place the CameraCapture source zip in this directory."
    exit 1
fi

echo "[1/5] Extracting $ZIP_FILE..."

# Remove old extracted files
if [ -d "$EXTRACT_DIR" ]; then
    echo "Removing old extracted files..."
    rm -rf "$EXTRACT_DIR"
fi

# Extract zip file
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
echo "Created: $LIB_OUTPUT_DIR"
echo "Created: $INCLUDE_OUTPUT_DIR"
echo ""

# 3. Configure CMake for shared library build
echo "[3/5] Configuring CameraCapture build with CMake..."
cd "$EXTRACT_DIR"

mkdir -p build
cd build

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCCAP_BUILD_SHARED=ON \
    -DCCAP_BUILD_TESTS=OFF \
    -DCCAP_BUILD_EXAMPLES=OFF \
    -DCCAP_BUILD_CLI=OFF \
    -DCCAP_INSTALL=OFF

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] CMake configuration failed!"
    echo ""
    echo "Please make sure you have all required development packages installed."
    echo ""
    cd ../..
    exit 1
fi

echo "[SUCCESS] CMake configuration completed"
echo ""

# 4. Build
echo "[4/5] Building CameraCapture shared library..."

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

# Copy shared library files (versioned .so)
if [ -f "libccap.so.1.7.0" ]; then
    cp -v "libccap.so.1.7.0" "../../$LIB_OUTPUT_DIR/"
fi

if [ -f "libccap.so.1" ]; then
    cp -v "libccap.so.1" "../../$LIB_OUTPUT_DIR/"
fi

if [ -f "libccap.so" ]; then
    cp -v "libccap.so" "../../$LIB_OUTPUT_DIR/"
fi

# Create symlinks if versioned .so exists
cd "../../$LIB_OUTPUT_DIR"
SO_VERSIONED=$(ls libccap.so.*.*.* 2>/dev/null | head -n1)
if [ -n "$SO_VERSIONED" ]; then
    # Extract major version (e.g. libccap.so.1.7.0 -> 1)
    SO_MAJOR=$(echo "$SO_VERSIONED" | sed 's/libccap\.so\.\([0-9]*\)\..*/\1/')
    ln -sf "$SO_VERSIONED" "libccap.so.$SO_MAJOR" 2>/dev/null || true
    ln -sf "libccap.so.$SO_MAJOR" "libccap.so" 2>/dev/null || true
    echo "Created symlinks: libccap.so.$SO_MAJOR -> $SO_VERSIONED"
    echo "                  libccap.so -> libccap.so.$SO_MAJOR"
fi
cd "$SCRIPT_DIR"

cd "$EXTRACT_DIR/build"

# Copy pkg-config file if exists
if [ -f "ccap.pc" ]; then
    cp -v "ccap.pc" "../../$LIB_OUTPUT_DIR/"
fi

cd ../..

# Copy header files
echo ""
echo "Copying header files..."
cp -v "$EXTRACT_DIR/include/"*.h "$INCLUDE_OUTPUT_DIR/"

echo ""
echo "========================================"
echo "[SUCCESS] CameraCapture build completed!"
echo "========================================"
echo ""
echo "Output files:"
echo "  Library: $LIB_OUTPUT_DIR/libccap.so  (symlink)"
echo "           $LIB_OUTPUT_DIR/libccap.so.1  (symlink)"
echo "           $LIB_OUTPUT_DIR/libccap.so.1.7.0"
echo "  Headers: $INCLUDE_OUTPUT_DIR/ccap.h"
echo "           $INCLUDE_OUTPUT_DIR/ccap_*.h"
echo ""
echo "Usage in your project:"
echo "  1. Compile: g++ -c your_code.cpp -I$INCLUDE_OUTPUT_DIR"
echo "  2. Link: g++ your_code.o -L$LIB_OUTPUT_DIR -lccap -lpthread"
echo "  3. Include: #include <ccap.h>"
echo ""
echo "Runtime library path:"
echo "  Add to LD_LIBRARY_PATH: export LD_LIBRARY_PATH=\$PWD/$LIB_OUTPUT_DIR:\$LD_LIBRARY_PATH"
echo "  Or install to system: sudo cp $LIB_OUTPUT_DIR/libccap.so* /usr/local/lib/"
echo "                        sudo ldconfig"
echo ""
echo "Cleaning up temporary files..."
rm -rf "$EXTRACT_DIR"
echo "Done!"
echo ""