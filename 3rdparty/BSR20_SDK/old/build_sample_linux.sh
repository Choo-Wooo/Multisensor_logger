#!/bin/bash
cd "$(dirname "$0")"

echo "========================================"
echo " BSR20 SDK Sample - Linux Build"
echo "========================================"
echo

echo "[1/2] Configuring with CMake..."
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] CMake configuration failed!"
    echo
    echo "Please check:"
    echo "  1. CMake is installed     (sudo apt install cmake)"
    echo "  2. Build tools installed  (sudo apt install build-essential)"
    echo "  3. linux/sdk/ directory exists"
    echo
    exit 1
fi
echo "[OK] Configuration successful"
echo

echo "[2/2] Building... (parallel: $(nproc) cores)"
cmake --build build --parallel "$(nproc)"
if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Build failed!"
    exit 1
fi

echo
echo "========================================"
echo " Build completed successfully!"
echo "========================================"
echo
echo "Output:"
echo "  build/BSR20_Sample"
echo
echo "Run:"
echo "  ./build/BSR20_Sample [radar_ip] [port]"
echo "  ./build/BSR20_Sample 192.168.172.128 7"
echo
