#!/bin/bash
set -e

# ========================================
echo "OpenSSL 3.5.6 Linux Shared Library Build Script"
echo "(GCC, Shared Libraries, lib/linux + include)"
echo "========================================"
echo

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Dependency check
echo "[0/5] Checking dependencies..."
if ! command -v perl &> /dev/null; then
    echo "[ERROR] Perl not found! Install it with: sudo apt-get install perl"
    exit 1
fi

if ! command -v make &> /dev/null; then
    echo "[ERROR] make not found! Install it with: sudo apt-get install build-essential"
    exit 1
fi

echo "[SUCCESS] Dependencies found"
echo

# Source and output paths
ZIP_FILE="openssl-openssl-3.5.6.zip"
EXTRACT_DIR="openssl-openssl-3.5.6"
BUILD_DIR="$EXTRACT_DIR/build"
LIB_OUTPUT_DIR="lib/linux"

# 1. Check and extract ZIP
if [ ! -f "$ZIP_FILE" ]; then
    echo "[ERROR] $ZIP_FILE not found!"
    exit 1
fi

echo "[1/5] Extracting $ZIP_FILE..."
if [ -d "$EXTRACT_DIR" ]; then
    rm -rf "$EXTRACT_DIR"
fi
unzip -q "$ZIP_FILE"
if [ ! -d "$EXTRACT_DIR" ]; then
    echo "[ERROR] Failed to extract $ZIP_FILE"
    exit 1
fi
echo "[SUCCESS] Extraction done"
echo

# 2. Create output directories
mkdir -p "$LIB_OUTPUT_DIR/bin"
mkdir -p "$LIB_OUTPUT_DIR/lib"
mkdir -p "$LIB_OUTPUT_DIR/include"
echo "[2/5] Output directories ready"
echo

# 3. OpenSSL Configure + Make Build
cd "$EXTRACT_DIR"

echo "[3/5] Configuring OpenSSL for Linux 32-bit shared libraries..."
# shared = shared libraries, linux-generic32 + -m32 = 32-bit Linux
./Configure linux-generic32 -m32 shared --prefix="$(pwd)/build"
if [ $? -ne 0 ]; then
    echo "[ERROR] Configure failed"
    exit 1
fi

echo "Building OpenSSL (this may take several minutes)..."
make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed"
    exit 1
fi

make install_sw
if [ $? -ne 0 ]; then
    echo "[ERROR] Install failed"
    exit 1
fi

echo "[SUCCESS] OpenSSL build completed"
echo

# 4. Copy shared libraries and binaries
echo "[4/5] Copying shared libraries to $LIB_OUTPUT_DIR..."
# Try lib64 first, then lib
if [ -d "build/lib64" ] && [ -f "build/lib64/libcrypto.so.3" ]; then
    cp -Lv build/lib64/libcrypto.so* "../$LIB_OUTPUT_DIR/lib/"
    cp -Lv build/lib64/libssl.so* "../$LIB_OUTPUT_DIR/lib/"
elif [ -d "build/lib" ] && [ -f "build/lib/libcrypto.so.3" ]; then
    cp -Lv build/lib/libcrypto.so* "../$LIB_OUTPUT_DIR/lib/"
    cp -Lv build/lib/libssl.so* "../$LIB_OUTPUT_DIR/lib/"
else
    echo "[ERROR] Could not find libcrypto.so or libssl.so in build/lib or build/lib64"
    exit 1
fi

if [ -d "build/bin" ]; then
    cp -v build/bin/* "../$LIB_OUTPUT_DIR/bin/" || true
fi

echo "[SUCCESS] Shared libraries copied"
echo

# 5. Copy headers
echo "[5/5] Copying headers to $LIB_OUTPUT_DIR/include..."
cp -r build/include/* "../$LIB_OUTPUT_DIR/include/"
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to copy headers"
    exit 1
fi
echo "[SUCCESS] Headers copied"
echo

# 6. Cleanup
cd ..
if [ -d "$EXTRACT_DIR" ]; then
    rm -rf "$EXTRACT_DIR"
fi
echo "Done!"
echo
echo "========================================"
echo "[SUCCESS] OpenSSL 3.5.6 Linux shared library build completed!"
echo "========================================"
echo
