#!/bin/bash
# 2채널 RTSP 로깅 하네스 빌드 (build_x86_64 정적 라이브러리 재사용)
set -e
R="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
B="${BUILD_DIR:-$R/build_x86_64}"
A="${MSL_ARCH:-x86_64}"
TP="$R/3rdparty"
g++ -std=c++17 -O2 -o "$B/bin/test_dual_camera" "$R/tools/test_dual_camera.cpp" \
  -I"$R/src" -I"$TP/spdlog/include" -I"$TP/FFmpeg/include" \
  -I"$TP/live555/include/liveMedia" -I"$TP/live555/include/UsageEnvironment" \
  -I"$TP/live555/include/BasicUsageEnvironment" -I"$TP/live555/include/groupsock" \
  -Wl,--start-group "$B/lib/libsensors.a" "$B/lib/libloggers.a" "$B/lib/libcore.a" -Wl,--end-group \
  "$TP/live555/lib/linux/$A/libliveMedia.a" "$TP/live555/lib/linux/$A/libgroupsock.a" \
  "$TP/live555/lib/linux/$A/libBasicUsageEnvironment.a" "$TP/live555/lib/linux/$A/libUsageEnvironment.a" \
  -L"$TP/FFmpeg/lib/linux/$A" -lavcodec -lavutil -lswscale -lavformat \
  -L"$TP/OpenSSL/lib/linux/lib/$A" -lssl -lcrypto \
  -Wl,-rpath,"$TP/FFmpeg/lib/linux/$A" -Wl,-rpath,"$TP/OpenSSL/lib/linux/lib/$A" \
  -lpthread -ldl
echo "built: $B/bin/test_dual_camera"
