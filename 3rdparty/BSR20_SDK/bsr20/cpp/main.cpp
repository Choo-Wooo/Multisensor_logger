#include "Bsr20Sdk.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------
// Relay/RD Callbacks
// Called automatically by SDK when connected to a relay-capable
// radar (0x55AA handshake). To disable, comment out the
// byda_set_relay_callback / byda_set_rd_callback calls below.
// ------------------------------------------------------------

static void onRelayTrack(BydaRadarHandle /*handle*/,
                         const BydaScanInfo* scan,
                         const BydaRelayTrack* relay,
                         void* /*ctx*/)
{
    printf("  [RELAY] scan=#%u  detect=%d  cnt=%d  range=%.1fm  vel=%.1fkph  pw=%d\n",
           scan->scan_index, relay->is_detect, relay->obj_count,
           relay->range, relay->velocity, relay->power);
}

static void onRdTracks(BydaRadarHandle /*handle*/,
                       const BydaScanInfo* scan,
                       const BydaRdTrack* tracks,
                       int count,
                       void* /*ctx*/)
{
    printf("  [RD] scan=#%u  count=%d\n", scan->scan_index, count);
    for (int i = 0; i < count; i++) {
        if (tracks[i].velocity == 0.0f && tracks[i].power == 0)
            continue;  // skip empty entries
        printf("    [%3d] vel=%.1fkph  pw=%d\n",
               i, tracks[i].velocity, tracks[i].power);
    }
}

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

        // Register relay/RD callbacks (only fires on relay-capable radar)
        // To disable: comment out these two lines
        byda_set_relay_callback(handle_, onRelayTrack, nullptr);
        byda_set_rd_callback(handle_, onRdTracks, nullptr);

        if (!connectRadar()) {
            return;
        }

        // Check relay mode after connect
        relayMode_ = (byda_is_relay_mode(handle_) != 0);
        if (relayMode_) {
            printf("[INFO] Relay mode detected. Track output via callbacks.\n");
        }

        // Load install parameters first (standard BSR20 only)
        if (!relayMode_) {
            byda_load_install(handle_);
        }

        printf("[INFO] Streaming started. Press Ctrl+C to exit.\n\n");

        int cycle = 0;
        while (running_) {
            int bytes = byda_receive(handle_);
            if (bytes <= 0) continue;

            int trackCount = byda_process(handle_);
            cycle++;

            if (!relayMode_) {
                // Handle parameter loading sequence (standard BSR20 only)
                handleParamLoading(cycle);

                // Print object tracks (standard BSR20 only)
                if (trackCount > 0) {
                    printTracks(trackCount);
                }

            }
            // In relay mode, output is handled by onRelayTrack / onRdTracks callbacks
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

        // Sequential parameter loading
        switch (paramStep_) {
        case 0: // Waiting for Install response
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

        case 1: // Waiting for Lane response
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

        case 2: // Waiting for Detect response
            if (cycle > 25) {
                float detLine = 0;
                if (byda_get_detect_line(handle_, &detLine) == BYDA_OK) {
                    printf("[Detect] Line=%.1fm\n", detLine);
                    printf("--------------------------------------------------\n");
                }
                paramStep_ = 3; // Done
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
    bool relayMode_ = false;
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
