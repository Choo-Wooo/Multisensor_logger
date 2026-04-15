#!/bin/bash
echo "===================================="
echo " BSR20 SDK Samples - Linux Build"
echo "===================================="
echo ""
echo "Targets:"
echo "  - bsr20_sample      (console)"
echo "  - hdr20_sample      (console)"
echo "  - bsr20_vds_viewer  (GUI, ImGui+OpenGL)"
echo ""
echo "Note: VDS Viewer requires: libgl-dev libx11-dev libxrandr-dev"
echo "      libxinerama-dev libxcursor-dev libxi-dev"
echo ""

# Select platform
echo "Select build platform:"
echo "  1. x86 (32-bit)"
echo "  2. x64 (64-bit)"
echo ""
read -p "Enter choice (1 or 2): " CHOICE

case "$CHOICE" in
    1)
        BITS="32"
        CMAKE_EXTRA="-DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32"
        ;;
    *)
        BITS="64"
        CMAKE_EXTRA=""
        ;;
esac

echo ""
echo "[INFO] Selected platform: x${BITS}"
echo ""

BUILD_DIR="build-x${BITS}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"

cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DSDK_BITS=${BITS} $CMAKE_EXTRA
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed"
    exit 1
fi

make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed"
    exit 1
fi

echo ""
echo "[OK] Build complete! (x${BITS})"
echo ""
echo "Output:"
echo "  ${BUILD_DIR}/bsr20_sample"
echo "  ${BUILD_DIR}/hdr20_sample"
echo "  ${BUILD_DIR}/bsr20/cpp_vds_viewer/bsr20_vds_viewer"
