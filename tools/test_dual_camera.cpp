// tools/test_dual_camera.cpp
// 2채널 RTSP 동시 로깅 검증용 헤드리스 하네스.
// GUI 없이 MainWindow 와 같은 경로(CameraRtspWorker -> CameraLogger)를 채널마다
// 하나씩 돌려서, 4K RTSP 2대를 동시에 녹화할 수 있는지 확인한다.
//
// 빌드: tools/build_test_dual_camera.sh  (build_x86_64 의 정적 라이브러리 재사용)
#include "core/app_config.h"
#include "core/clock.h"
#include "core/event_bus.h"
#include "sensors/camera_rtsp_worker.h"
#include "loggers/camera_logger.h"

#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <sys/resource.h>

using namespace msl;

int main(int argc, char** argv) {
    const std::string cfg_path = (argc > 1) ? argv[1] : "config.ini";
    const int seconds = (argc > 2) ? std::atoi(argv[2]) : 60;
    const std::string out_dir = (argc > 3) ? argv[3] : "/tmp/msl_dualcam";

    if (const char* dbg = std::getenv("MSL_DEBUG")) {
        if (dbg[0] == '1') spdlog::set_level(spdlog::level::debug);   // CameraLogger perf 출력
    }

    AppConfig config;
    if (!config.load(cfg_path)) {
        spdlog::error("config load failed: {}", cfg_path);
        return 1;
    }

    EventBus bus;
    std::unique_ptr<CameraRtspWorker> workers[AppConfig::kNumCameras];
    std::unique_ptr<CameraLogger>     loggers[AppConfig::kNumCameras];
    std::atomic<uint64_t> frames[AppConfig::kNumCameras];
    std::atomic<uint64_t> audio[AppConfig::kNumCameras];
    std::atomic<uint64_t> nals[AppConfig::kNumCameras];
    double nal_pts[AppConfig::kNumCameras] = {};
    std::atomic<bool>     connected[AppConfig::kNumCameras];
    double last_pts[AppConfig::kNumCameras] = {};
    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        frames[i] = 0; audio[i] = 0; nals[i] = 0; connected[i] = false;
    }

    const double t0 = Clock::now();
    std::atomic<bool> recording{false};
    double rec_t0 = 0.0;                    // 녹화 시작 시각(PC 시계)
    FILE* csv = fopen((out_dir + "/frames.csv").c_str(), "w");
    if (csv) fprintf(csv, "cam,callback_rel,pts\n");

    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        const auto& c = config.cameras[i];
        if (!c.enabled || c.type != "RTSP" || c.rtsp_url.empty()) {
            spdlog::info("cam{}: skipped (enabled={}, type={})", i + 1, c.enabled, c.type);
            continue;
        }
        std::string path = out_dir + "/cam" + std::to_string(i + 1) + ".mov";
        const bool passthrough = !std::getenv("MSL_ENCODE");   // 기본: 패스스루
        loggers[i] = std::make_unique<CameraLogger>(
            path, c.width, c.height, c.fps,
            passthrough ? CameraLogger::Mode::Passthrough : CameraLogger::Mode::Encode);
        if (!loggers[i]->start()) {
            spdlog::error("cam{}: logger start failed ({})", i + 1, path);
            return 2;
        }

        workers[i] = std::make_unique<CameraRtspWorker>(bus, c.rtsp_url, c.width, c.height);
        workers[i]->on_video_nal = [&, i](CameraNalPacket&& np) {
            if (recording && loggers[i] &&
                loggers[i]->mode() == CameraLogger::Mode::Passthrough) {
                nal_pts[i] = np.pc_ts_rel;
                nals[i]++;
                loggers[i]->writeVideoPacket(std::move(np));
            }
        };
        workers[i]->on_frame_ready = [&, i](const CameraFrame& f) {
            if (recording && loggers[i]) {
                if (loggers[i]->mode() == CameraLogger::Mode::Passthrough)
                    loggers[i]->setSourceSize(f.srcWidth(), f.srcHeight());
                else
                    loggers[i]->writeFrame(f);
                last_pts[i] = f.pc_ts_rel;
                // 싱크 측정용: 콜백 시각(PC 시계, 녹화 시작 기준)과 기록된 PTS 를 남긴다.
                if (csv) fprintf(csv, "%d,%.6f,%.6f\n", i + 1, Clock::now() - rec_t0, f.pc_ts_rel);
            }
            frames[i]++;
        };
        workers[i]->on_audio_packet = [&, i](const CameraAudioPacket& p) {
            if (recording && loggers[i]) loggers[i]->writeAudioPacket(p);
            audio[i]++;
        };
        workers[i]->on_connection_changed = [&, i](bool up) { connected[i] = up; };
        workers[i]->on_error = [i](const std::string& m) {
            spdlog::error("cam{}: {}", i + 1, m);
        };
        workers[i]->start();
        spdlog::info("cam{}: connecting {} ({}x{}@{})", i + 1, c.rtsp_url, c.width, c.height, c.fps);
    }

    // 연결 후 대기 시간(초). RTCP 동기가 붙은 뒤 녹화를 시작해 보고 싶을 때 사용.
    if (const char* d = std::getenv("MSL_DELAY_SEC")) {
        int wait_s = std::atoi(d);
        spdlog::info("연결 후 {}초 대기 (RTCP 동기 대기)", wait_s);
        std::this_thread::sleep_for(std::chrono::seconds(wait_s));
    }

    // 첫 프레임이 들어올 때까지 대기 후 녹화 시작 (MainWindow::startRecording 과 동일한 순서)
    for (int w = 0; w < 100; ++w) {
        bool any = false;
        for (int i = 0; i < AppConfig::kNumCameras; ++i) if (frames[i] > 0) any = true;
        if (any) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    rec_t0 = Clock::now();
    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        if (!workers[i]) continue;
        workers[i]->setRecording(true, rec_t0);
        workers[i]->resetRecordingClock();
        frames[i] = 0;
    }
    recording = true;
    spdlog::info("=== recording {}s ===", seconds);

    uint64_t prev[AppConfig::kNumCameras] = {};
    for (int s = 1; s <= seconds; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        bus.drain();
        struct rusage ru; getrusage(RUSAGE_SELF, &ru);
        double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 +
                     ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
        if (s % 5 == 0 || s == 1) {
            printf("[%3ds] cam1 %5llu (%4.1f fps)  cam2 %5llu (%4.1f fps)  cpu=%.0f%% rss=%ldMB\n",
                   s,
                   (unsigned long long)frames[0].load(),
                   (frames[0].load() - prev[0]) / (s == 1 ? 1.0 : 5.0),
                   (unsigned long long)frames[1].load(),
                   (frames[1].load() - prev[1]) / (s == 1 ? 1.0 : 5.0),
                   cpu / (Clock::now() - t0) * 100.0, ru.ru_maxrss / 1024);
            fflush(stdout);
            prev[0] = frames[0].load(); prev[1] = frames[1].load();
        }
    }

    recording = false;
    const double elapsed = Clock::now() - rec_t0;
    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        if (workers[i]) { workers[i]->setRecording(false); workers[i]->stopRtsp(); }
    }
    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        if (loggers[i]) { loggers[i]->stop(); }
    }

    if (csv) fclose(csv);
    printf("\n=== result (%.1fs) ===\n", elapsed);
    for (int i = 0; i < AppConfig::kNumCameras; ++i) {
        if (!workers[i]) continue;
        printf("cam%d: 프리뷰 %llu프레임(%.2f fps) | 기록 %llu AU(%.2f fps, last pts=%.3fs)"
               " | audio %llu\n",
               i + 1, (unsigned long long)frames[i].load(), frames[i].load() / elapsed,
               (unsigned long long)nals[i].load(), nals[i].load() / elapsed, nal_pts[i],
               (unsigned long long)audio[i].load());
    }
    return 0;
}
