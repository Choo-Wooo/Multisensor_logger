#pragma once

#include "core/app_config.h"
#include "core/app_state.h"
#include "core/event_bus.h"
#include "ui/settings_panel.h"
#include "ui/player_panel.h"
#include "ui/camera_view.h"
#include "ui/sensor_info_view.h"
#include "ui/bev_view.h"

#include "sensors/sensor_worker.h"
#include "sensors/ouster_worker.h"
#include "sensors/radar20_worker.h"
#include "sensors/radar30_worker.h"
#include "sensors/camera_rtsp_worker.h"
#include "sensors/camera_usb_worker.h"
#include "sensors/gps_worker.h"

#include "core/latest_value.h"
#include "player/session_player.h"
#include "loggers/base_logger.h"
#include "loggers/lidar_logger.h"
#include "loggers/radar_logger.h"
#include "loggers/camera_logger.h"
#include "loggers/gps_logger.h"
#include "loggers/imu_logger.h"
#include "loggers/session_logger.h"

#include <memory>

namespace msl {

/// Top-level application orchestrator.
class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow() = default;

    void init();
    void update();
    void shutdown();

    EventBus& eventBus() { return event_bus_; }

private:
    AppConfig config_;
    AppState  state_;
    EventBus  event_bus_;

    // UI panels
    SettingsPanel  settings_panel_;
    PlayerPanel    player_panel_;
    CameraView     camera_view_;
    SensorInfoView sensor_info_view_;
    BevView        bev_view_;

    // Sensor workers (nullptr when disconnected)
    std::unique_ptr<OusterWorker>      ouster_worker_;
    std::unique_ptr<ISensorWorker>     radar_worker_;   // Radar20 or Radar30
    std::unique_ptr<ISensorWorker>     camera_worker_;  // RTSP or USB
    std::unique_ptr<GpsWorker>         gps_worker_;

    // Latest sensor data (lock-free, no EventBus for large data)
    LatestValue<CameraFrame>   latest_camera_;
    LatestValue<LidarScanData> latest_lidar_;

    // Player
    std::unique_ptr<SessionPlayer> session_player_;

    // Loggers (created during recording, destroyed on stop)
    std::unique_ptr<LidarLogger>    lidar_logger_;
    std::unique_ptr<RadarLogger>    radar_logger_;
    std::unique_ptr<CameraLogger>   camera_logger_;
    std::unique_ptr<GpsLogger>      gps_logger_;
    std::unique_ptr<ImuLogger>      imu_logger_;

    void renderModeToggle();
    void renderLoggingLayout();
    void renderPlayerLayout();
    void renderStatusBar();
    void handleDevModeShortcut();
    void renderDevModePopup();

    void connectSettingsCallbacks();
    void connectPlayerCallbacks();

    void toggleLidar();
    void toggleRadar();
    void toggleCamera();
    void toggleGps();
    void startRecording();
    void stopRecording();

    // Wire worker callbacks to UI
    void wireOusterCallbacks();
    void wireRadarCallbacks();
    void wireCameraCallbacks();
    void wireGpsCallbacks();

    bool dev_mode_enabled_ = false;
    bool dev_popup_requested_ = false;
    bool dev_password_focus_requested_ = false;
    char dev_password_buf_[32] = "";
    std::string dev_mode_error_;
};

} // namespace msl
