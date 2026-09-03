// tools/bench_player.cpp — 플레이어 프레임 접근 성능 측정
// 라이다 10Hz 타임라인에 맞춰 카메라 2채널 프레임을 순서대로 요청하며 시간을 잰다.
#include "player/camera_prefetch.h"
#include "core/clock.h"
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
using namespace msl;

static int findNearest(const std::vector<double>& ts, double t) {
    if (ts.empty()) return -1;
    auto it = std::lower_bound(ts.begin(), ts.end(), t);
    if (it == ts.end()) return (int)ts.size() - 1;
    if (it == ts.begin()) return 0;
    auto pv = it - 1;
    return (std::abs(*it - t) < std::abs(*pv - t)) ? (int)(it - ts.begin()) : (int)(pv - ts.begin());
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: bench_player <cam1.mov> <cam2.mov> [frames]\n"); return 1; }
    const int nframes = (argc > 3) ? std::atoi(argv[3]) : 200;

    CameraPrefetch p1, p2;
    if (!p1.open(argv[1]) || !p2.open(argv[2])) { printf("open failed\n"); return 2; }
    const auto ts1 = p1.timestamps(), ts2 = p2.timestamps();
    printf("cam1 %d frames, cam2 %d frames\n", p1.frameCount(), p2.frameCount());

    // 라이다 기준(10Hz) 타임라인을 흉내낸다
    double t0 = std::max(ts1.front(), ts2.front());
    CameraFrame f;
    double worst = 0, total = 0;
    int decoded = 0;
    for (int i = 0; i < nframes; ++i) {
        double t = t0 + i * 0.1;
        double b = Clock::steady();
        int i1 = findNearest(ts1, t), i2 = findNearest(ts2, t);
        if (i1 >= 0) { p1.getFrame(i1, f); decoded++; }
        if (i2 >= 0) { p2.getFrame(i2, f); decoded++; }
        double ms = (Clock::steady() - b) * 1000.0;
        total += ms;
        if (ms > worst) worst = ms;
    }
    printf("타임라인 %d 스텝(2채널): 평균 %.1f ms/스텝, 최대 %.1f ms, 총 %.2f s\n",
           nframes, total / nframes, worst, total / 1000.0);
    printf("→ 재생 가능 속도: 약 %.1f 배속 (10Hz 기준)\n", 100.0 / (total / nframes));
    return 0;
}
