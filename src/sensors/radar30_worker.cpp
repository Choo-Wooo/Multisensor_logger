#include "radar30_worker.h"
#include <spdlog/spdlog.h>

// BSR30 SDK
#include <Bsr30Sdk.h>

namespace msl {

// BSR30 callback has no user_data parameter — use a file-scope static pointer
static Radar30Worker* g_radar30_instance = nullptr;

static void staticFrameCallback(const bsr30_frame_t* frame) {
    if (!g_radar30_instance || !frame) return;

    RadarScanData scan;
    scan.scan_index = frame->sequence;
    scan.timestamp  = frame->timestamp;

    for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
        const auto& t = frame->tracks[i];
        if (t.pw <= 0) continue;  // Skip inactive tracks

        RadarTrack track;
        track.id    = t.id;
#ifdef _WIN32
        track.x_pos = t.xPos_m;
        track.y_pos = t.yPos_m;
        track.x_vel = t.xVel_kph;
        track.y_vel = t.yVel_kph;
#else
        // Linux BSR30 SDK renames these fields (binary layout unchanged)
        track.x_pos = t.xPos_pred_m;
        track.y_pos = t.yPos_pred_m;
        track.x_vel = t.xVel_pred_kph;
        track.y_vel = t.yVel_pred_kph;
#endif
        track.type  = t.vehicleType;
        track.status = 0;
        scan.tracks.push_back(track);
    }

    g_radar30_instance->pushScan(std::move(scan));
}

void Radar30Worker::pushScan(RadarScanData scan) {
    callback_queue_.try_push(std::move(scan));
}

bool Radar30Worker::onConnect() {
    g_radar30_instance = this;

    bsr30_set_radar_frame_callback(staticFrameCallback);

    bool ok = bsr30_connect(ip_.c_str(), tcp_port_, udp_port_);
    if (!ok) {
        notifyError("Radar30: Connect failed to " + ip_);
        g_radar30_instance = nullptr;
        return false;
    }

    ok = bsr30_radar_start();
    if (!ok) {
        notifyError("Radar30: Failed to start radar stream");
        bsr30_disconnect();
        g_radar30_instance = nullptr;
        return false;
    }

    spdlog::info("Radar30: Connected to {}:{}/{}", ip_, tcp_port_, udp_port_);
    return true;
}

void Radar30Worker::pollLoop() {
    while (running_ && !disconnect_requested_) {
        RadarScanData scan;
        if (callback_queue_.try_pop(scan, std::chrono::milliseconds(100))) {
            if (recording_) {
                scan.pc_ts_rel = getRelativeTime();
            }

            frame_count_++;

            if (on_scan_ready) {
                auto cb = on_scan_ready;
                event_bus_.post([cb, scan]() { cb(scan); });
            }
        }
    }
}

void Radar30Worker::onDisconnect() {
    bsr30_radar_stop();
    bsr30_disconnect();
    g_radar30_instance = nullptr;
    callback_queue_.clear();
    spdlog::info("Radar30: Disconnected");
}

} // namespace msl
