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
    std::atomic<bool> camera_connected{false};
    std::atomic<bool> gps_connected{false};

    // Sensor frame counts (updated by workers)
    std::atomic<uint64_t> lidar_frames{0};
    std::atomic<uint64_t> radar_frames{0};
    std::atomic<uint64_t> camera_frames{0};
    std::atomic<uint64_t> gps_frames{0};
    std::atomic<uint64_t> imu_frames{0};

    void resetFrameCounts() {
        lidar_frames = 0;
        radar_frames = 0;
        camera_frames = 0;
        gps_frames = 0;
        imu_frames = 0;
    }

    bool anySensorConnected() const {
        return lidar_connected || radar_connected ||
               camera_connected || gps_connected;
    }
};

} // namespace msl
