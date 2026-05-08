#include "Bsr30Sdk.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <string>

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------
// Radar Application (RAII style)
// ------------------------------------------------------------
class RadarApp {
public:
    RadarApp(std::string ip, int tcp, int udp)
        : radarIp_(std::move(ip))
        , tcpPort_(tcp)
        , udpPort_(udp) {

        }

    ~RadarApp() {
        stop();
    }

    void run() {
        installSignalHandler();

        printSdkVersion();

        bsr30_set_radar_frame_callback(onRadarFrameStatic);

        if (!connectAndStart()) {
            printf("[ERROR] Failed to start radar.\n");
            return;
        }

        watchdogThread_ = std::thread([this] { 
            watchdogLoop();
        });

        printf("[INFO] Press Ctrl+C to exit.\n");

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        stop();
    }

private:
    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------
    void stop() {
        if (!running_) {
            return;
        }

        running_ = false;

        bsr30_radar_stop();
        bsr30_disconnect();

        if (watchdogThread_.joinable()) {
            watchdogThread_.join();
        }

        printf("\n[INFO] Total frames received: %u\n", frameCount_.load());
        printf("[INFO] Shutdown complete.\n");
    }

    bool connectAndStart() {
        // SDK의 TCP connect timeout(5초, NetworkManager) + Windows ConnectEx 취소 지연(최대 ~17초)을
        // 합산하여 여유 있게 25초 대기. retry count 기반은 OS 지연에 취약하므로 time-based로 변경.
        constexpr long long CONNECT_DEADLINE_MS = 25000; // bsr30_connect 후 최대 대기
        constexpr int       RETRY_DELAY_MS      = 1000;  // 재연결 시도 간격
        constexpr int       START_RETRY_MS      = 500;   // radar_start 폴링 간격

        while (running_) {
            printf("[INFO] Connecting to %s (TCP:%d UDP:%d)\n", radarIp_.c_str(), tcpPort_, udpPort_);

            if (!bsr30_connect(radarIp_.c_str(), tcpPort_, udpPort_)) {
                printf("[WARN] Connect failed. Retrying in %d s...\n", RETRY_DELAY_MS / 1000);
                sleepMs(RETRY_DELAY_MS);
                continue;
            }

            // bsr30_connect() 성공 후 TCP 연결 완료까지 time-based 대기
            const long long deadline = nowMs() + CONNECT_DEADLINE_MS;
            bool started = false;

            while (running_ && nowMs() < deadline) {
                if (bsr30_radar_start()) {
                    started = true;
                    break;
                }
                long long remaining = (deadline - nowMs()) / 1000;
                printf("[INFO] Waiting TCP ready... (%llds left)\n", remaining);
                sleepMs(START_RETRY_MS);
            }

            if (started) {
                printf("[INFO] Streaming started.\n\n");
                lastFrameMs_ = nowMs();
                return true;
            }

            printf("[WARN] Radar start timeout. Reconnecting...\n");
            bsr30_disconnect();
            sleepMs(RETRY_DELAY_MS);
        }

        return false;
    }

    void reconnect() {
        printf("[WARN] No frame detected. Reconnecting...\n");

        bsr30_radar_stop();
        bsr30_disconnect();

        connectAndStart();
    }

    // ------------------------------------------------------------
    // Watchdog
    // ------------------------------------------------------------
    void watchdogLoop() {
        constexpr long long WATCHDOG_MS = 5000;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            auto elapsed = nowMs() - lastFrameMs_.load();
            if (elapsed > WATCHDOG_MS) {
                reconnect();
                lastFrameMs_ = nowMs();
            }
        }
    }

    // ------------------------------------------------------------
    // Frame Callback
    // ------------------------------------------------------------
    static void onRadarFrameStatic(const bsr30_frame_t* frame) {
        instance_->onRadarFrame(frame);
    }

    static inline bool isActiveTrack(const bsr30_track_t& t) {
        return t.id != 0 || t.pw > 0;
    }

    void onRadarFrame(const bsr30_frame_t* frame) {
        lastFrameMs_ = nowMs();
        const uint32_t frameNo = ++frameCount_;

        int active = 0;
        for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
            if (isActiveTrack(frame->tracks[i])) {
                ++active;
            }
        }

        printf("=== FRAME #%u | seq=%u | ts=%u ms | active=%d/%d ===\n", frameNo, frame->sequence, frame->timestamp, active, BSR30_TRACK_COUNT);

        for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
            const auto& t = frame->tracks[i];
            if (!isActiveTrack(t)) {
                continue;
            }

            printf(" [%02d] id=%u  pw=%u  spFlag=0x%08X  initVY=%.1fkph\n"
                "       pos(%.2f, %.2f)m  vel(%.1f, %.1f)kph\n"
                "       lane=%d  initLane=%d  type=%u\n",
                i,
                static_cast<unsigned>(t.id),
                static_cast<unsigned>(t.pw),
                t.spFlag,
                t.initPosVY_x1kph,
                t.xPos_pred_1xM, t.yPos_pred_1xM,
                t.xVel_pred_1xKph, t.yVel_pred_1xKph,
                static_cast<int>(t.laneNum),
                static_cast<int>(t.initLaneNum),
                static_cast<unsigned>(t.vehicleType));
        }

        if (active == 0) {
            printf("  (no active tracks)\n");
        }

        printf("--------------------------------------------------\n");
    }

    // ------------------------------------------------------------
    // Utility
    // ------------------------------------------------------------
    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }

    static void sleepMs(int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    static void signalHandler(int) {
        instance_->running_ = false;
    }

    void installSignalHandler() {
        instance_ = this;
        std::signal(SIGINT,  signalHandler);
        std::signal(SIGTERM, signalHandler);
    }

    void printSdkVersion() {
        bsr30_sdk_version_t ver{};
        bsr30_sdk_get_version(&ver);

        printf("====================================\n");
        printf("  %s v%d.%d.%d\n", ver.name, ver.major, ver.minor, ver.patch);
        printf("  %s\n", ver.manufacturer);
        printf("====================================\n\n");
    }

private:
    std::string radarIp_;
    int tcpPort_;
    int udpPort_;

    std::atomic<bool> running_{true};
    std::atomic<uint32_t> frameCount_{0};
    std::atomic<long long> lastFrameMs_{0};

    std::thread watchdogThread_;

    static RadarApp* instance_;
};

RadarApp* RadarApp::instance_ = nullptr;

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string ip = "192.168.172.128";
    int tcpPort = 8088;
    int udpPort = 9002;

    RadarApp app(ip, tcpPort, udpPort);
    app.run();

    return 0;
}