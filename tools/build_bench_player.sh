#!/bin/bash
set -e
R="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
B="${BUILD_DIR:-$R/build_x86_64}"; A="${MSL_ARCH:-x86_64}"; TP="$R/3rdparty"
g++ -std=c++17 -O2 -o "$B/bin/bench_player" "$R/tools/bench_player.cpp" \
  -I"$R/src" -I"$TP/spdlog/include" -I"$TP/FFmpeg/include" \
  -Wl,--start-group "$B/lib/libplayer.a" "$B/lib/libcore.a" -Wl,--end-group \
  -L"$TP/FFmpeg/lib/linux/$A" -lavcodec -lavutil -lswscale -lavformat -lswresample \
  -L"$TP/ouster-sdk/lib/linux/$A" -lshared_library \
  -Wl,-rpath,"$TP/FFmpeg/lib/linux/$A" -Wl,-rpath,"$TP/ouster-sdk/lib/linux/$A" \
  -Wl,--disable-new-dtags -lpthread -ldl
echo "built: $B/bin/bench_player"
