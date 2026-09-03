#!/bin/bash
# =============================================================================
# 3rdparty x86_64 (Ubuntu 20.04 / gcc 9) 재빌드 스크립트
#
# tools/build_3rdparty_aarch64.sh 의 x86_64 판. 저장소에는 윈도우용 바이너리와
# aarch64 빌드 결과만 들어 있어 x86_64 리눅스에서는 링크가 불가능하다.
# 이 스크립트는 의존성을 x86_64 로 빌드해 CMakeLists 가 기대하는 경로
# (3rdparty/<pkg>/lib/linux/x86_64 등)에 설치한다.
#
#   OpenSSL 3.0        -> 3rdparty/OpenSSL/lib/linux/lib/x86_64/
#   FFmpeg n8.1.2      -> 3rdparty/FFmpeg/lib/linux/x86_64/       (avcodec.62 / avutil.60 / swscale.9)
#   GLFW 3.4           -> 3rdparty/glfw/lib/linux/x86_64/
#   Dear ImGui         -> 3rdparty/imgui/lib/linux/x86_64/        (커밋 7d8d587bc)
#   libnmea 0.1.2      -> 3rdparty/libnmea/linux/sdk/lib/x86_64/
#   live555            -> 3rdparty/live555/lib/linux/x86_64/
#   ouster-sdk 0.16.1  -> 3rdparty/ouster-sdk/lib/linux/x86_64/libshared_library.so
#
# BYDA 레이더 SDK(BSR20/BSR30)는 tools/build_byda_sdk_x86_64.sh 참고.
# =============================================================================
set -e

REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
TP="$REPO_DIR/3rdparty"
WORK="${WORK_DIR:-$HOME/build_x86}"
JOBS="$(nproc)"
mkdir -p "$WORK"

step() { echo; echo "=================== $* ==================="; }

# -----------------------------------------------------------------------------
step "[1/7] OpenSSL 3.0 (Ubuntu 20.04 는 1.1.1 뿐이라 별도 빌드)"
cd "$WORK"
if [ ! -f "$WORK/ssl_install/lib64/libssl.so.3" ] && [ ! -f "$WORK/ssl_install/lib/libssl.so.3" ]; then
    [ -d openssl ] || git clone --depth 1 --branch openssl-3.0.15 https://github.com/openssl/openssl.git
    cd openssl
    ./Configure linux-x86_64 shared no-tests --prefix="$WORK/ssl_install" --libdir=lib
    make -j"$JOBS"
    make install_sw
    cd "$WORK"
fi
mkdir -p "$TP/OpenSSL/lib/linux/lib/x86_64"
cp -P "$WORK"/ssl_install/lib/libssl.so* "$WORK"/ssl_install/lib/libcrypto.so* \
      "$TP/OpenSSL/lib/linux/lib/x86_64/"

# -----------------------------------------------------------------------------
step "[2/7] nasm + x264 (PIC 정적) + FFmpeg n8.1.2 (shared)"
cd "$WORK"
# Ubuntu 20.04 의 nasm 2.14 는 FFmpeg 8.x 의 x86 asm 을 어셈블하지 못한다.
if [ ! -x "$WORK/install/bin/nasm" ]; then
    [ -f nasm-2.16.03.tar.gz ] || wget -q https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.gz
    rm -rf nasm-2.16.03 && tar xf nasm-2.16.03.tar.gz
    cd nasm-2.16.03 && ./configure --prefix="$WORK/install" > /dev/null && make -j"$JOBS" > /dev/null && make install > /dev/null
    cd "$WORK"
fi
export PATH="$WORK/install/bin:$PATH"
# Ubuntu 20.04 x86_64 에는 libx264-dev(amd64) 가 없을 수 있어 소스에서 빌드한다.
# 정적(-fPIC)으로 만들어 libavcodec.so 안에 그대로 링크되게 한다(런타임 의존 없음).
if [ ! -f "$WORK/x264_install/lib/libx264.a" ]; then
    [ -d x264 ] || git clone --depth 1 --branch stable https://code.videolan.org/videolan/x264.git
    cd x264
    ./configure --prefix="$WORK/x264_install" --enable-static --enable-pic --disable-cli
    make -j"$JOBS"
    make install
    cd "$WORK"
fi
export PKG_CONFIG_PATH="$WORK/x264_install/lib/pkgconfig:$PKG_CONFIG_PATH"

if [ ! -f "$WORK/ffmpeg_install/lib/libavcodec.so.62" ]; then
    [ -d FFmpeg ] || git clone --depth 1 --branch n8.1.2 https://github.com/FFmpeg/FFmpeg.git
    cd FFmpeg
    ./configure --prefix="$WORK/ffmpeg_install" --enable-shared --disable-static \
        --disable-doc --disable-programs --enable-gpl --enable-libx264 --disable-debug \
        --extra-cflags="-I$WORK/x264_install/include" \
        --extra-ldflags="-L$WORK/x264_install/lib"
    make -j"$JOBS"
    make install
    cd "$WORK"
fi
mkdir -p "$TP/FFmpeg/lib/linux/x86_64"
cp -P "$WORK"/ffmpeg_install/lib/lib*.so* "$TP/FFmpeg/lib/linux/x86_64/"

# -----------------------------------------------------------------------------
step "[3/7] GLFW 3.4"
cd "$WORK"
[ -d glfw ] || git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git
cmake -S glfw -B glfw/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF > /dev/null
cmake --build glfw/build -j"$JOBS" > /dev/null
mkdir -p "$TP/glfw/lib/linux/x86_64"
cp -P glfw/build/src/libglfw.so* "$TP/glfw/lib/linux/x86_64/"

# -----------------------------------------------------------------------------
step "[4/7] Dear ImGui (번들 헤더와 동일한 커밋 7d8d587bc)"
cd "$WORK"
[ -d imgui ] || git clone https://github.com/ocornut/imgui.git
cd imgui && git checkout -q 7d8d587bc && cd ..
rm -rf imgui_build && mkdir -p imgui_build && cd imgui_build
g++ -O2 -fPIC -std=c++17 -c ../imgui/imgui.cpp ../imgui/imgui_draw.cpp ../imgui/imgui_tables.cpp \
    ../imgui/imgui_widgets.cpp ../imgui/imgui_demo.cpp \
    ../imgui/backends/imgui_impl_glfw.cpp ../imgui/backends/imgui_impl_opengl3.cpp \
    -I../imgui -I../imgui/backends -I"$TP/glfw/include"
g++ -shared -Wl,-soname,libimgui.so.1 -o libimgui.so.1.0.0 *.o \
    -L"$TP/glfw/lib/linux/x86_64" -lglfw -lGL -ldl -lpthread
mkdir -p "$TP/imgui/lib/linux/x86_64"
cp libimgui.so.1.0.0 "$TP/imgui/lib/linux/x86_64/"
ln -sf libimgui.so.1.0.0 "$TP/imgui/lib/linux/x86_64/libimgui.so.1"
ln -sf libimgui.so.1 "$TP/imgui/lib/linux/x86_64/libimgui.so"
cd "$WORK"

# -----------------------------------------------------------------------------
step "[5/7] libnmea"
cd "$WORK"
[ -d libnmea ] || git clone --depth 1 https://github.com/jacketizer/libnmea.git
cmake -S libnmea -B libnmea/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="$WORK/nmea_install" > /dev/null
cmake --build libnmea/build -j"$JOBS" > /dev/null
cmake --install libnmea/build > /dev/null
mkdir -p "$TP/libnmea/linux/sdk/lib/x86_64"
cp -P "$WORK/nmea_install/lib/libnmea.so"* "$TP/libnmea/linux/sdk/lib/x86_64/"
cp "$WORK/nmea_install/lib/nmea/"*.a       "$TP/libnmea/linux/sdk/lib/x86_64/" 2>/dev/null || true

# -----------------------------------------------------------------------------
step "[6/7] live555 (NO_STD_LIB: gcc 9 는 C++20 std::atomic_flag::test() 미지원)"
cd "$WORK"
[ -d live555git ] || git clone -q https://github.com/rgaufman/live555.git live555git
rm -rf live555b && cp -r live555git live555b && cd live555b
sed -i 's|^COMPILE_OPTS =\t\t|COMPILE_OPTS =\t\t-fPIC -DNO_STD_LIB=1 |' config.linux
sed -i 's/-std=c++20/-std=c++17/' config.linux
./genMakefiles linux
make -j"$JOBS" > "$WORK/live555_make.log" 2>&1
mkdir -p "$TP/live555/lib/linux/x86_64"
cp liveMedia/libliveMedia.a groupsock/libgroupsock.a \
   UsageEnvironment/libUsageEnvironment.a BasicUsageEnvironment/libBasicUsageEnvironment.a \
   "$TP/live555/lib/linux/x86_64/"
cd "$WORK"

# -----------------------------------------------------------------------------
step "[7/7] Eigen 3.4 + ouster-sdk 0.16.1 (shared_library)"
cd "$WORK"
[ -d eigen ] || git clone --depth 1 --branch 3.4.0 https://gitlab.com/libeigen/eigen.git
cmake -S eigen -B eigen/build -DCMAKE_INSTALL_PREFIX="$WORK/install" > /dev/null
cmake --build eigen/build --target install > /dev/null

[ -d ouster-sdk ] || git clone --depth 1 --branch v0.16.1 https://github.com/ouster-lidar/ouster-sdk.git
cmake -S ouster-sdk -B ouster-sdk/build -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBRARY=ON -DBUILD_VIZ=OFF -DBUILD_MAPPING=OFF -DBUILD_OSF=OFF \
    -DBUILD_PCAP=OFF -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="$WORK/install"
cmake --build ouster-sdk/build -j"$JOBS"
mkdir -p "$TP/ouster-sdk/lib/linux/x86_64"
cp ouster-sdk/build/ouster_library/libshared_library.so "$TP/ouster-sdk/lib/linux/x86_64/"

echo
echo "완료. 이제 본체를 빌드하세요:"
echo "  cmake -S . -B build_x86_64 -DCMAKE_BUILD_TYPE=Release && cmake --build build_x86_64 -j"
