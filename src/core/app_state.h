#pragma once

#include <atomic>
#include <string>
#include <mutex>

namespace msl {

enum class AppMode {
    Logging,
    Player
};

struct AppState {
    // Application mode
    std::atomic<AppMode> current_mode{AppMode::Logging};

    // Recording state
    std::atomic<bool> is_recording{false};
    std::atomic<double> recording_start_time{0.0};  // Unix timestamp
    std::string session_dir;
    std::string session_name;
    std::mutex session_mutex;

    // Sensor connection states
    std::atomic<bool> lidar_connected{false};
    std::atomic<bool> radar_connected{false};
    /// BSR30 only — true after the user issues "Start Radar" and frames begin
    /// to flow. Independent of `radar_connected` (you can be connected but
    /// not streaming).
    std::atomic<bool> radar_streaming{false};
    /// True while the radar worker is attempting to recover after a frame
    /// timeout (BSR30 watchdog re-entry). UI shows "Reconnecting..." while set.
    std::atomic<bool> radar_reconnecting{false};
    /// Per-camera-channel connection state (see AppConfig::kNumCameras).
    static constexpr int kNumCameras = 2;
    std::atomic<bool> camera_connected[kNumCameras] = {};
    std::atomic<bool> gps_connected{false};

    // Sensor frame counts (updated by workers)
    std::atomic<uint64_t> lidar_frames{0};
    std::atomic<uint64_t> radar_frames{0};
    std::atomic<uint64_t> camera_frames[kNumCameras] = {};
    std::atomic<uint64_t> gps_frames{0};
    std::atomic<uint64_t> imu_frames{0};

    void resetFrameCounts() {
        lidar_frames = 0;
        radar_frames = 0;
        for (auto& c : camera_frames) c = 0;
        gps_frames = 0;
        imu_frames = 0;
    }

    /// True when at least one camera channel is connected.
    bool anyCameraConnected() const {
        for (const auto& c : camera_connected)
            if (c.load()) return true;
        return false;
    }

    /// Total frames received across all camera channels (status bar).
    uint64_t totalCameraFrames() const {
        uint64_t sum = 0;
        for (const auto& c : camera_frames) sum += c.load();
        return sum;
    }

    bool anySensorConnected() const {
        return lidar_connected || radar_connected ||
               anyCameraConnected() || gps_connected;
    }
};

} // namespace msl
