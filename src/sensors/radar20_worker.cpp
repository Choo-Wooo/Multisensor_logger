#include "radar20_worker.h"
#include <spdlog/spdlog.h>

// BSR20 SDK C API
#include <Bsr20Sdk.h>

namespace msl {

bool Radar20Worker::onConnect() {
    handle_ = byda_create();
    if (!handle_) {
        notifyError("Radar20: Failed to create handle");
        return false;
    }

    byda_set_address(handle_, ip_.c_str(), port_);

    int ret = byda_connect(handle_);
    if (ret != BYDA_OK) {
        notifyError("Radar20: Connect failed (code " + std::to_string(ret) + ")");
        byda_destroy(handle_);
        handle_ = nullptr;
        return false;
    }

    // Load installation parameters
    byda_load_install(handle_);
    for (int i = 0; i < 50 && !byda_is_param_idle(handle_); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("Radar20: Connected to {}:{}", ip_, port_);
    return true;
}

void Radar20Worker::pollLoop() {
    while (running_ && !disconnect_requested_) {
        int received = byda_receive(handle_);
        if (received <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int track_count = byda_process(handle_);

        RadarScanData scan;

        // Get scan info
        BydaScanInfo info;
        byda_get_scan_info(handle_, &info);
        scan.scan_index = info.scan_index;
        scan.timestamp  = info.timestamp;
        scan.error_flag = info.error_flag;

        if (recording_) {
            scan.pc_ts_rel = getRelativeTime();
        }

        // Get tracks
        for (int i = 0; i < track_count; ++i) {
            BydaTrack bt;
            byda_get_track(handle_, i, &bt);

            RadarTrack t;
            t.id     = bt.id;
            t.x_pos  = bt.x_pos;
            t.y_pos  = bt.y_pos;
            t.x_vel  = bt.x_vel;
            t.y_vel  = bt.y_vel;
            t.type   = bt.type;
            t.status = bt.status;
            scan.tracks.push_back(t);
        }

        frame_count_++;

        if (on_scan_ready) {
            auto cb = on_scan_ready;
            event_bus_.post([cb, scan]() { cb(scan); });
        }
    }
}

void Radar20Worker::onDisconnect() {
    if (handle_) {
        byda_disconnect(handle_);
        byda_destroy(handle_);
        handle_ = nullptr;
    }
    spdlog::info("Radar20: Disconnected");
}

} // namespace msl
