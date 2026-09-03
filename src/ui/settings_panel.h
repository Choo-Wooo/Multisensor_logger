#pragma once

#include "core/app_config.h"
#include "core/app_state.h"
#include <string>
#include <vector>
#include <functional>

namespace msl {

/// Left panel: sensor settings, connection controls, recording controls.
class SettingsPanel {
public:
    // Callbacks for MainWindow to handle
    std::function<void()> on_lidar_toggle;
    std::function<void()> on_radar_toggle;
    /// BSR30 only: Start/Stop streaming after Connect (separate from connect).
    std::function<void()> on_radar_start_toggle;
    /// Connect/disconnect a camera channel (argument = channel index).
    std::function<void(int)> on_camera_toggle;
    std::function<void()> on_gps_toggle;
    std::function<void()> on_start_recording;
    std::function<void()> on_stop_recording;
    /// Test an RTSP URL for a channel (index, url).
    std::function<void(int, const std::string& url)> on_rtsp_test;

    void render(AppConfig& config, AppState& state);

    /// Called when an RTSP test completes with the detected resolution.
    void setRtspTestResult(int idx, bool success, int width, int height,
                           const std::string& message);

private:
    // Session name input buffer
    char session_name_buf_[128] = "";
    char data_dir_buf_[256] = "";
    bool initialized_ = false;

    // RTSP test state (per camera channel)
    static constexpr int kNumCameras = AppConfig::kNumCameras;
    int resolution_preset_ = 0;
    std::string rtsp_test_status_[kNumCameras];
    int native_width_[kNumCameras] = {};
    int native_height_[kNumCameras] = {};

    // Webcam device list
    std::vector<std::string> webcam_devices_;
    int webcam_device_idx_ = 0;

    // FPS calculation
    double last_fps_time_ = 0.0;
    uint64_t prev_lidar_ = 0, prev_radar_ = 0, prev_gps_ = 0, prev_imu_ = 0;
    uint64_t prev_camera_[kNumCameras] = {};
    float fps_lidar_ = 0, fps_radar_ = 0, fps_gps_ = 0, fps_imu_ = 0;
    float fps_camera_[kNumCameras] = {};

    void renderLidarSection(AppConfig& config, AppState& state);
    void renderRadarSection(AppConfig& config, AppState& state);
    void renderCameraSection(AppConfig& config, AppState& state, int idx);
    void renderGpsSection(AppConfig& config, AppState& state);
    void renderRecordingSection(AppConfig& config, AppState& state);

    void renderStatusIndicator(bool connected);
};

} // namespace msl
