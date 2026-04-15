#!/bin/bash
# =============================================================================
# FFmpeg 8.0.1 Linux Build Script
# (live555 RTSP stream decoding - minimal build)
#
# Output:
#   lib/linux/   -- libavcodec.so.62, libavutil.so.60, libswscale.so.9
#   include/     -- libavcodec/ libavutil/ libswscale/ headers
#
# Enabled decoders: h264, hevc, mpeg4, mjpeg, rawvideo
# Enabled parsers : h264, hevc, mpeg4video
# Enabled BSF     : h264_mp4toannexb, hevc_mp4toannexb
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SOURCE_FILE="ffmpeg-8.0.1.tar.xz"
EXTRACT_DIR="ffmpeg-8.0.1"
LIB_OUTPUT_DIR="lib/linux"
INCLUDE_OUTPUT_DIR="include"

CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "========================================"
echo "FFmpeg 8.0.1 Linux Build Script"
echo "(live555 RTSP stream decoding - minimal)"
echo "========================================"
echo ""

# ---------------------------------------------------------
# [0/4] Dependency check
# ---------------------------------------------------------
echo "[0/4] Checking dependencies..."
echo ""

require_cmd() {
    local cmd="$1"
    local hint="$2"
    if command -v "$cmd" &>/dev/null; then
        echo "[OK] $cmd: $(command -v "$cmd")"
    else
        echo ""
        echo "[ERROR] '$cmd' not found!"
        echo "  Install: $hint"
        echo ""
        exit 1
    fi
}

require_cmd gcc    "sudo apt install gcc  / sudo yum install gcc"
require_cmd make   "sudo apt install make / sudo yum install make"

# NASM (optional)
HAS_NASM=0
if command -v nasm &>/dev/null; then
    echo "[OK] NASM: $(command -v nasm)"
    HAS_NASM=1
else
    echo "[INFO] NASM not found - x86 ASM optimizations disabled"
    echo "       Install: sudo apt install nasm"
fi

# Source tarball
if [ ! -f "$SOURCE_FILE" ]; then
    echo ""
    echo "[ERROR] $SOURCE_FILE not found in $SCRIPT_DIR"
    echo "  Download: https://ffmpeg.org/releases/$SOURCE_FILE"
    echo ""
    exit 1
fi

# Detect host architecture
HOST_ARCH=$(uname -m)
case "$HOST_ARCH" in
    x86_64)          FFMPEG_ARCH="x86_64" ;;
    i686|i386)       FFMPEG_ARCH="x86" ;;
    aarch64|arm64)   FFMPEG_ARCH="aarch64" ;;
    armv7*|armhf)    FFMPEG_ARCH="arm" ;;
    *)               FFMPEG_ARCH="$HOST_ARCH" ;;
esac
echo "[OK] Architecture: $HOST_ARCH -> FFmpeg --arch=$FFMPEG_ARCH"
echo "[OK] CPU cores: $CORES"
echo ""

# ---------------------------------------------------------
# [1/4] Extract source
# ---------------------------------------------------------
echo "[1/4] Extracting $SOURCE_FILE..."

if [ -d "$EXTRACT_DIR" ]; then
    echo "[INFO] Removing old extraction: $EXTRACT_DIR"
    rm -rf "$EXTRACT_DIR"
fi

tar -xf "$SOURCE_FILE"

if [ ! -d "$EXTRACT_DIR" ]; then
    echo "[ERROR] Extraction failed: directory $EXTRACT_DIR not found"
    exit 1
fi

echo "[OK] Extracted: $EXTRACT_DIR"
echo ""

# ---------------------------------------------------------
# [2/4] Create output directories
# ---------------------------------------------------------
echo "[2/4] Creating output directories..."
mkdir -p "$LIB_OUTPUT_DIR"
mkdir -p "$INCLUDE_OUTPUT_DIR"
echo "[OK] Directories ready"
echo ""

# ---------------------------------------------------------
# [3/4] Configure + Build
# ---------------------------------------------------------
echo "[3/4] Configuring FFmpeg (gcc, arch=$FFMPEG_ARCH)..."
echo ""

cd "$EXTRACT_DIR"

CFG="./configure"
CFG="$CFG --prefix=./ffmpeg_output"
CFG="$CFG --enable-shared"
CFG="$CFG --disable-static"
CFG="$CFG --disable-programs"
CFG="$CFG --disable-doc"
CFG="$CFG --disable-avdevice"
CFG="$CFG --disable-avformat"
CFG="$CFG --disable-swresample"
CFG="$CFG --disable-avfilter"
CFG="$CFG --disable-network"
CFG="$CFG --disable-everything"
CFG="$CFG --enable-avcodec"
CFG="$CFG --enable-avutil"
CFG="$CFG --enable-swscale"
CFG="$CFG --enable-decoder=h264"
CFG="$CFG --enable-decoder=hevc"
CFG="$CFG --enable-decoder=mpeg4"
CFG="$CFG --enable-decoder=mjpeg"
CFG="$CFG --enable-decoder=rawvideo"
CFG="$CFG --enable-parser=h264"
CFG="$CFG --enable-parser=hevc"
CFG="$CFG --enable-parser=mpeg4video"
CFG="$CFG --enable-bsf=h264_mp4toannexb"
CFG="$CFG --enable-bsf=hevc_mp4toannexb"
CFG="$CFG --arch=$FFMPEG_ARCH"
CFG="$CFG --enable-pic"   # Required for shared libraries

if [ "$HAS_NASM" -eq 0 ]; then
    CFG="$CFG --disable-x86asm"
    echo "[INFO] x86 ASM: disabled (install nasm for better performance)"
else
    echo "[INFO] x86 ASM: enabled"
fi

echo "[INFO] Running configure..."
echo ""
eval "$CFG"

echo ""
echo "[OK] Configure completed"
echo ""

echo "[4/4] Building with $CORES cores (this may take several minutes)..."
echo ""
make -j"$CORES"
make install

cd "$SCRIPT_DIR"

echo ""
echo "[OK] Build completed"
echo ""

# ---------------------------------------------------------
# [5/4] Copy output files
# ---------------------------------------------------------
BUILD_OUTPUT="$EXTRACT_DIR/ffmpeg_output"

echo "[INFO] Copying shared libraries to $LIB_OUTPUT_DIR/..."
# Copy all versioned .so files and their unversioned symlinks
find "$BUILD_OUTPUT/lib" \( -name "libavcodec*.so*" \
                         -o -name "libavutil*.so*"  \
                         -o -name "libswscale*.so*" \) \
    | while read -r f; do
        if [ -L "$f" ]; then
            # Preserve symlinks
            cp -Pv "$f" "$LIB_OUTPUT_DIR/"
        else
            cp -v "$f" "$LIB_OUTPUT_DIR/"
        fi
    done

echo ""
echo "[INFO] Copying headers to $INCLUDE_OUTPUT_DIR/..."
mkdir -p "$INCLUDE_OUTPUT_DIR/libavcodec"
mkdir -p "$INCLUDE_OUTPUT_DIR/libavutil"
mkdir -p "$INCLUDE_OUTPUT_DIR/libswscale"
cp -r "$BUILD_OUTPUT/include/libavcodec/." "$INCLUDE_OUTPUT_DIR/libavcodec/"
cp -r "$BUILD_OUTPUT/include/libavutil/."  "$INCLUDE_OUTPUT_DIR/libavutil/"
cp -r "$BUILD_OUTPUT/include/libswscale/." "$INCLUDE_OUTPUT_DIR/libswscale/"
echo "  Copied: libavcodec/ libavutil/ libswscale/"

echo ""
echo "[INFO] Cleaning up extracted source..."
rm -rf "$EXTRACT_DIR"

echo ""
echo "========================================"
echo "[SUCCESS] FFmpeg 8.0.1 Linux build done!"
echo "========================================"
echo ""
echo "Output:"
echo "  $LIB_OUTPUT_DIR/   -- libavcodec.so.62, libavutil.so.60, libswscale.so.9"
echo "  $INCLUDE_OUTPUT_DIR/     -- header files"
echo ""
echo "Enabled decoders: h264, hevc, mpeg4, mjpeg, rawvideo"
echo "Enabled parsers : h264, hevc, mpeg4video"
echo "Enabled BSF     : h264_mp4toannexb, hevc_mp4toannexb"
echo ""
