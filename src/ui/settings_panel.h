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
    std::function<void()> on_camera_toggle;
    std::function<void()> on_gps_toggle;
    std::function<void()> on_start_recording;
    std::function<void()> on_stop_recording;
    std::function<void(const std::string& url)> on_rtsp_test;  // Test RTSP connection

    void render(AppConfig& config, AppState& state);

    /// Called when RTSP test completes with detected resolution.
    void setRtspTestResult(bool success, int width, int height, const std::string& message);

private:
    // Session name input buffer
    char session_name_buf_[128] = "";
    char data_dir_buf_[256] = "";
    bool initialized_ = false;

    // RTSP test state
    int resolution_preset_ = 0;
    std::string rtsp_test_status_;
    int native_width_ = 0;
    int native_height_ = 0;

    // Webcam device list
    std::vector<std::string> webcam_devices_;
    int webcam_device_idx_ = 0;

    // FPS calculation
    double last_fps_time_ = 0.0;
    uint64_t prev_lidar_ = 0, prev_radar_ = 0, prev_camera_ = 0, prev_gps_ = 0, prev_imu_ = 0;
    float fps_lidar_ = 0, fps_radar_ = 0, fps_camera_ = 0, fps_gps_ = 0, fps_imu_ = 0;

    void renderLidarSection(AppConfig& config, AppState& state);
    void renderRadarSection(AppConfig& config, AppState& state);
    void renderCameraSection(AppConfig& config, AppState& state);
    void renderGpsSection(AppConfig& config, AppState& state);
    void renderRecordingSection(AppConfig& config, AppState& state);

    void renderStatusIndicator(bool connected);
};

} // namespace msl
