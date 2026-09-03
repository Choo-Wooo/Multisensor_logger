#!/bin/bash
# =============================================================================
# 3rdparty aarch64 (Jetson Orin / Ubuntu 20.04) 재빌드 스크립트
#
# 리포지토리에 들어 있는 리눅스용 프리빌트 바이너리는 전부 x86(i386/x86-64)이라
# aarch64 에서는 링크가 불가능하다. 이 스크립트는 오픈소스 의존성을 aarch64 로
# 다시 빌드해 3rdparty/<pkg>/lib/linux-aarch64 (및 그에 준하는 경로)에 설치한다.
#
#   ouster-sdk 0.16.1 -> 3rdparty/ouster-sdk/lib/linux-aarch64/libshared_library.so
#   GLFW 3.4          -> 3rdparty/glfw/lib/linux-aarch64/
#   Dear ImGui 1.92.5 -> 3rdparty/imgui/lib/linux-aarch64/       (커밋 7d8d587bc)
#   libnmea 0.1.2     -> 3rdparty/libnmea/linux-aarch64/sdk/
#   live555           -> 3rdparty/live555/lib/linux-aarch64/ + include-aarch64/
#   OpenSSL 3         -> 3rdparty/OpenSSL/lib/linux-aarch64/    (/usr/local 사용)
#   FFmpeg            -> 재빌드하지 않고 배포판 시스템 패키지(pkg-config) 사용
#
# BYDA 레이더 SDK(BSR20/BSR30)는 각 SDK 저장소에서 별도로 빌드한다:
#   byda_sdk_v2.0 : cmake -B build_aarch64 -DCMAKE_BUILD_TYPE=Release && cmake --build build_aarch64 -j
#                   -> build_aarch64/sdk/{lib,include}  =>  3rdparty/BSR20_SDK/linux-aarch64/sdk/64bit/
#   bsr30_sdk     : 3rdparty/libuv 를 aarch64 로 먼저 빌드(lib/linux-aarch64/async) 후 동일하게 빌드
#                   -> build_aarch64/sdk/{lib,include}  =>  3rdparty/BSR30_SDK/linux-aarch64/sdk/
# =============================================================================
set -e

REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
TP="$REPO_DIR/3rdparty"
WORK="${WORK_DIR:-$HOME/build_arm}"
mkdir -p "$WORK"

echo "[0/6] apt 의존성 설치"
sudo apt-get update -y
sudo apt-get install -y build-essential git pkg-config cmake patchelf \
    libeigen3-dev libcurl4-openssl-dev libssl-dev libpcap-dev libtins-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libglu1-mesa-dev xorg-dev

# -----------------------------------------------------------------------------
echo "[1/6] Eigen 3.4 (ouster-sdk 빌드 전용, 격리 prefix - 시스템/ROS Eigen 3.3 은 건드리지 않음)"
cd "$WORK"
[ -d eigen ] || git clone --depth 1 --branch 3.4.0 https://gitlab.com/libeigen/eigen.git
cmake -S eigen -B eigen/build -DCMAKE_INSTALL_PREFIX="$WORK/install" > /dev/null
cmake --build eigen/build --target install > /dev/null

# -----------------------------------------------------------------------------
echo "[2/6] ouster-sdk 0.16.1 (shared_library)"
cd "$WORK"
[ -d ouster-sdk ] || git clone --depth 1 --branch v0.16.1 https://github.com/ouster-lidar/ouster-sdk.git
cmake -S ouster-sdk -B ouster-sdk/build -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBRARY=ON -DBUILD_VIZ=OFF -DBUILD_MAPPING=OFF -DBUILD_OSF=OFF \
    -DBUILD_PCAP=ON -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="$WORK/install"
cmake --build ouster-sdk/build -j"$(nproc)"
mkdir -p "$TP/ouster-sdk/lib/linux-aarch64"
cp ouster-sdk/build/ouster_library/libshared_library.so "$TP/ouster-sdk/lib/linux-aarch64/"

# -----------------------------------------------------------------------------
echo "[3/6] GLFW 3.4"
cd "$WORK"
[ -d glfw ] || git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git
cmake -S glfw -B glfw/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
cmake --build glfw/build -j"$(nproc)"
mkdir -p "$TP/glfw/lib/linux-aarch64"
cp -P glfw/build/src/libglfw.so* "$TP/glfw/lib/linux-aarch64/"

# -----------------------------------------------------------------------------
echo "[4/6] Dear ImGui 1.92.5 WIP (번들 헤더와 동일한 커밋 7d8d587bc)"
cd "$WORK"
[ -d imgui ] || git clone https://github.com/ocornut/imgui.git
cd imgui && git checkout -q 7d8d587bc && cd ..
rm -rf imgui_build && mkdir -p imgui_build && cd imgui_build
g++ -O2 -fPIC -std=c++17 -c ../imgui/imgui.cpp ../imgui/imgui_draw.cpp ../imgui/imgui_tables.cpp \
    ../imgui/imgui_widgets.cpp ../imgui/imgui_demo.cpp \
    ../imgui/backends/imgui_impl_glfw.cpp ../imgui/backends/imgui_impl_opengl3.cpp \
    -I../imgui -I../imgui/backends -I"$TP/glfw/include"
g++ -shared -Wl,-soname,libimgui.so.1 -o libimgui.so.1.0.0 *.o \
    -L"$TP/glfw/lib/linux-aarch64" -lglfw -lGL -ldl -lpthread
mkdir -p "$TP/imgui/lib/linux-aarch64"
cp libimgui.so.1.0.0 "$TP/imgui/lib/linux-aarch64/"
ln -sf libimgui.so.1.0.0 "$TP/imgui/lib/linux-aarch64/libimgui.so.1"
ln -sf libimgui.so.1 "$TP/imgui/lib/linux-aarch64/libimgui.so"
cd "$WORK"

# -----------------------------------------------------------------------------
echo "[5/6] libnmea"
cd "$WORK"
[ -d libnmea ] || git clone --depth 1 https://github.com/jacketizer/libnmea.git
cmake -S libnmea -B libnmea/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="$WORK/nmea_install"
cmake --build libnmea/build -j"$(nproc)"
cmake --install libnmea/build
mkdir -p "$TP/libnmea/linux-aarch64/sdk/lib" "$TP/libnmea/linux-aarch64/sdk/include"
cp -P "$WORK/nmea_install/lib/libnmea.so"* "$TP/libnmea/linux-aarch64/sdk/lib/"
cp "$WORK/nmea_install/lib/nmea/"*.a       "$TP/libnmea/linux-aarch64/sdk/lib/"
cp -r "$WORK/nmea_install/include/"*       "$TP/libnmea/linux-aarch64/sdk/include/"
# nmea_init/nmea_cleanup 선언이 들어 있는 사내 포크 헤더를 그대로 사용 (심볼은 upstream .so 에 존재)
cp "$TP/libnmea/linux/sdk/include/nmea.h"        "$TP/libnmea/linux-aarch64/sdk/include/"
cp "$TP/libnmea/linux/sdk/include/nmea_export.h" "$TP/libnmea/linux-aarch64/sdk/include/"

# -----------------------------------------------------------------------------
echo "[6/6] live555 (NO_STD_LIB 구성: gcc 9 는 C++20 std::atomic_flag::test() 미지원)"
cd "$WORK"
[ -d live555git ] || git clone -q https://github.com/rgaufman/live555.git live555git
rm -rf live555b && cp -r live555git live555b && cd live555b
sed -i 's|^COMPILE_OPTS =\t\t|COMPILE_OPTS =\t\t-fPIC -DNO_STD_LIB=1 |' config.linux
sed -i 's/-std=c++20/-std=c++17/' config.linux
./genMakefiles linux
make -j"$(nproc)"
mkdir -p "$TP/live555/lib/linux-aarch64"
cp liveMedia/libliveMedia.a groupsock/libgroupsock.a \
   UsageEnvironment/libUsageEnvironment.a BasicUsageEnvironment/libBasicUsageEnvironment.a \
   "$TP/live555/lib/linux-aarch64/"
rm -rf "$TP/live555/include-aarch64"
for d in liveMedia groupsock UsageEnvironment BasicUsageEnvironment; do
    mkdir -p "$TP/live555/include-aarch64/$d"
    cp $d/include/*.hh "$TP/live555/include-aarch64/$d/" 2>/dev/null || true
    cp $d/include/*.h  "$TP/live555/include-aarch64/$d/" 2>/dev/null || true
done

# -----------------------------------------------------------------------------
echo "[+] OpenSSL 3 (시스템 /usr/local 설치본 사용)"
mkdir -p "$TP/OpenSSL/lib/linux-aarch64/lib" "$TP/OpenSSL/lib/linux-aarch64/include"
cp -P /usr/local/lib/libssl.so* /usr/local/lib/libcrypto.so* "$TP/OpenSSL/lib/linux-aarch64/lib/"
cp -r /usr/local/include/openssl "$TP/OpenSSL/lib/linux-aarch64/include/"

echo ""
echo "완료. 이제 본체를 빌드하세요:"
echo "  cmake -S . -B build_aarch64 -DCMAKE_BUILD_TYPE=Release && cmake --build build_aarch64 -j"
echo "  ./package_linux.sh"
