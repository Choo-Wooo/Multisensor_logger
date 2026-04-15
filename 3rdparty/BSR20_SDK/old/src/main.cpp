#include "byda_c_api.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------
// Radar Application
// ------------------------------------------------------------
class RadarApp {
public:
    RadarApp(std::string ip, int port)
        : radarIp_(std::move(ip))
        , port_(port) {}

    ~RadarApp() { stop(); }

    void run() {
        installSignalHandler();

        printf("====================================\n");
        printf("  BSR20 SDK Sample\n");
        printf("  SDK Version: %.3f\n", byda_get_sdk_version());
        printf("====================================\n\n");

        handle_ = byda_create();
        if (!handle_) {
            printf("[ERROR] Failed to create radar context\n");
            return;
        }

        byda_set_address(handle_, radarIp_.c_str(), port_);

        if (!connectRadar()) {
            return;
        }

        // 파라미터 로드 순서
        byda_load_install(handle_);

        printf("[INFO] Streaming started. Press Ctrl+C to exit.\n\n");

        int cycle = 0;
        while (running_) {
            int bytes = byda_receive(handle_);
            if (bytes <= 0) continue;

            int trackCount = byda_process(handle_);
            cycle++;

            // 파라미터 응답 처리
            handleParamLoading(cycle);

            // 트랙 출력
            if (trackCount > 0) {
                printTracks(trackCount);
            }

            // VDS 이벤트 출력
            printVdsEvents();
        }

        stop();
    }

private:
    // ------------------------------------------------------------
    // Connection
    // ------------------------------------------------------------
    bool connectRadar() {
        printf("[INFO] Connecting to %s:%d...\n", radarIp_.c_str(), port_);

        int rc = byda_connect(handle_);
        if (rc != BYDA_OK) {
            printf("[ERROR] Connection failed (err=%d)\n", rc);
            return false;
        }

        printf("[INFO] Connected! Comm version: %d\n", byda_get_comm_version(handle_));
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;

        if (handle_) {
            byda_disconnect(handle_);
            byda_destroy(handle_);
            handle_ = nullptr;
        }

        printf("\n[INFO] Shutdown complete.\n");
    }

    // ------------------------------------------------------------
    // Parameter Loading
    // ------------------------------------------------------------
    void handleParamLoading(int cycle) {
        if (!byda_is_param_idle(handle_)) return;

        // 순차적 파라미터 로드
        switch (paramStep_) {
        case 0: // Install 응답 대기 중
            if (cycle > 5) {
                BydaInstallInfo info;
                if (byda_get_install_info(handle_, &info) == BYDA_OK) {
                    printf("--------------------------------------------------\n");
                    printf("[Install] H=%.1fm D=%.1fm Pitch=%.1f Azi=%.1f Dir=%d\n",
                           info.height, info.distance, info.pitch,
                           info.azimuth, info.direction);
                    printf("--------------------------------------------------\n");
                }
                byda_load_lane(handle_);
                paramStep_ = 1;
            }
            break;

        case 1: // Lane 응답 대기 중
            if (cycle > 15) {
                int laneCount = byda_get_lane_count(handle_);
                printf("--------------------------------------------------\n");
                for (int i = 0; i < laneCount; i++) {
                    BydaLaneInfo lane;
                    if (byda_get_lane_info(handle_, i, &lane) == BYDA_OK && lane.enable) {
                        printf("[Lane %d] Type=%d Dir=%d StPos=%.1fm W=%.1fm\n",
                               i + 1, lane.type, lane.direction,
                               lane.start_pos, lane.width);
                    }
                }
                printf("--------------------------------------------------\n");
                byda_load_detect(handle_);
                paramStep_ = 2;
            }
            break;

        case 2: // Detect 응답 대기 중
            if (cycle > 25) {
                float detLine = 0;
                if (byda_get_detect_line(handle_, &detLine) == BYDA_OK) {
                    printf("[Detect] Line=%.1fm\n", detLine);
                    printf("--------------------------------------------------\n");
                }
                paramStep_ = 3; // 완료
            }
            break;

        default:
            break;
        }
    }

    // ------------------------------------------------------------
    // Track Output
    // ------------------------------------------------------------
    void printTracks(int count) {
        BydaScanInfo scan;
        byda_get_scan_info(handle_, &scan);

        printf("=== SCAN #%u | ts=%u | tracks=%d ===\n",
               scan.scan_index, scan.timestamp, count);

        for (int i = 0; i < count; i++) {
            BydaTrack t;
            if (byda_get_track(handle_, i, &t) == BYDA_OK) {
                printf("  [%3d] pos(%6.1f, %6.1f)m  vel(%5.1f, %5.1f)  t=%d s=%d\n",
                       t.id, t.x_pos, t.y_pos,
                       t.x_vel, t.y_vel, t.type, t.status);
            }
        }
        printf("--------------------------------------------------\n");
    }

    // ------------------------------------------------------------
    // VDS Output
    // ------------------------------------------------------------
    void printVdsEvents() {
        int count = byda_get_vds_count(handle_);
        if (count <= 0) return;

        for (int i = 0; i < count; i++) {
            BydaVdsEvent ev;
            if (byda_get_vds_event(handle_, i, &ev) == BYDA_OK) {
                printf("  [VDS] Lane=%d ID=%d Speed=%.1fkph Type=%d\n",
                       ev.lane_num, ev.track_id, ev.speed, ev.track_type);
            }
        }
    }

    // ------------------------------------------------------------
    // Signal handling
    // ------------------------------------------------------------
    static void signalHandler(int) { instance_->running_ = false; }

    void installSignalHandler() {
        instance_ = this;
        std::signal(SIGINT,  signalHandler);
        std::signal(SIGTERM, signalHandler);
    }

private:
    std::string radarIp_;
    int port_;
    BydaRadarHandle handle_ = nullptr;
    std::atomic<bool> running_{true};
    int paramStep_ = 0;

    static RadarApp* instance_;
};

RadarApp* RadarApp::instance_ = nullptr;

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string ip = "192.168.172.128";
    int port = 7;

    if (argc >= 2) ip = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    RadarApp app(ip, port);
    app.run();

    return 0;
}
