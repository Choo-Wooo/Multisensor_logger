#!/bin/bash

# live555 Linux Static Library Build and Deploy Script

set -e  # Exit on error

echo "========================================"
echo "live555 2026.01.12 Static Library Build Script"
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
    echo "  Fedora:        sudo dnf install gcc gcc-c++"
    echo "  Arch:          sudo pacman -S gcc"
    echo ""
    exit 1
fi

echo "[SUCCESS] GCC found: $(gcc --version | head -n1)"
echo ""

# Path settings
TAR_GZ_FILE="live.2026.01.12.tar.gz"
EXTRACT_DIR="live"
LIB_OUTPUT_DIR="lib/linux"
INCLUDE_OUTPUT_DIR="include"

# 1. Check tar.gz file
if [ ! -f "$TAR_GZ_FILE" ]; then
    echo "[ERROR] $TAR_GZ_FILE not found!"
    echo "Please download live555 source from http://www.live555.com/liveMedia/"
    exit 1
fi

echo "[1/5] Extracting $TAR_GZ_FILE..."

# Remove old extracted files
if [ -d "$EXTRACT_DIR" ]; then
    echo "Removing old extracted files..."
    rm -rf "$EXTRACT_DIR"
fi

# Extract tar.gz file
tar -xzf "$TAR_GZ_FILE"

if [ ! -d "$EXTRACT_DIR" ]; then
    echo "[ERROR] Failed to extract $TAR_GZ_FILE"
    exit 1
fi

echo "[SUCCESS] Extraction completed"
echo ""

# Apply patches for modern compiler compatibility
echo "[1.5/5] Applying patches for modern compiler compatibility..."

# Fix atomic_flag::test() issue (C++20 compatibility)
# Replace .test() with proper implementation
sed -i 's/if (fTriggersAwaitingHandling\[i\]\.test()) {$/if (!fTriggersAwaitingHandling[i].test_and_set(std::memory_order_acq_rel)) {/g' "$EXTRACT_DIR/BasicUsageEnvironment/BasicTaskScheduler.cpp"
sed -i '/fTriggersAwaitingHandling\[i\]\.clear();/d' "$EXTRACT_DIR/BasicUsageEnvironment/BasicTaskScheduler.cpp"

echo "[SUCCESS] Patches applied"
echo ""

# 2. Create output directories
echo "[2/5] Creating output directories..."
mkdir -p "$LIB_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR"
echo "Created: $LIB_OUTPUT_DIR"
echo "Created: $INCLUDE_OUTPUT_DIR"
echo ""

# 3. Configure CMake
echo "[3/5] Configuring live555 build with CMake..."

mkdir -p build
cd build

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DCMAKE_CXX_FLAGS="-fpermissive -Wno-error=incompatible-pointer-types -std=c++2a -m32" \
    -DCMAKE_C_FLAGS="-Wno-error=incompatible-pointer-types -m32"

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] CMake configuration failed!"
    echo ""
    cd ..
    exit 1
fi

echo "[SUCCESS] CMake configuration completed"
echo ""

# 4. Build
echo "[4/5] Building live555 static libraries..."

NPROC=$(nproc 2>/dev/null || echo 4)
echo "Building with $NPROC parallel jobs..."

make -j$NPROC

if [ $? -ne 0 ]; then
    echo ""
    echo "[ERROR] Build failed!"
    echo ""
    cd ..
    exit 1
fi

echo "[SUCCESS] Build completed"
echo ""

# 5. Copy files
echo "[5/5] Copying files to output directories..."

cd ..

# Copy static library files
for LIB in libliveMedia.a libgroupsock.a libBasicUsageEnvironment.a libUsageEnvironment.a; do
    if [ -f "build/$LIB" ]; then
        cp -v "build/$LIB" "$LIB_OUTPUT_DIR/"
        echo "Copied $LIB to $LIB_OUTPUT_DIR"
    fi
done

# Copy header files
echo ""
echo "Copying header files..."
mkdir -p "$INCLUDE_OUTPUT_DIR/liveMedia"
mkdir -p "$INCLUDE_OUTPUT_DIR/groupsock"
mkdir -p "$INCLUDE_OUTPUT_DIR/BasicUsageEnvironment"
mkdir -p "$INCLUDE_OUTPUT_DIR/UsageEnvironment"

cp -v "$EXTRACT_DIR"/liveMedia/include/*.hh "$INCLUDE_OUTPUT_DIR/liveMedia/"
cp -v "$EXTRACT_DIR"/groupsock/include/*.hh "$INCLUDE_OUTPUT_DIR/groupsock/"
cp -v "$EXTRACT_DIR"/groupsock/include/*.h "$INCLUDE_OUTPUT_DIR/groupsock/"
cp -v "$EXTRACT_DIR"/BasicUsageEnvironment/include/*.hh "$INCLUDE_OUTPUT_DIR/BasicUsageEnvironment/"
cp -v "$EXTRACT_DIR"/UsageEnvironment/include/*.hh "$INCLUDE_OUTPUT_DIR/UsageEnvironment/"

echo ""
echo "========================================"
echo "[SUCCESS] live555 build completed!"
echo "========================================"
echo ""
echo "Output libraries: $LIB_OUTPUT_DIR/"
echo "  - libliveMedia.a"
echo "  - libgroupsock.a"
echo "  - libBasicUsageEnvironment.a"
echo "  - libUsageEnvironment.a"
echo ""
echo "Output headers: $INCLUDE_OUTPUT_DIR/"
echo "  - liveMedia/*.hh"
echo "  - groupsock/*.hh, *.h"
echo "  - BasicUsageEnvironment/*.hh"
echo "  - UsageEnvironment/*.hh"
echo ""
echo "Usage in your project:"
echo "  Link order: -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment"
echo ""
echo "Cleaning up temporary files..."
rm -rf "$EXTRACT_DIR"
rm -rf "build"
echo "Done!"
echo ""
