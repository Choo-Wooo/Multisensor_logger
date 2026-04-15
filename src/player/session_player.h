#pragma once

#include "data_loader.h"
#include "camera_prefetch.h"
#include "pcap_reader.h"
#include "core/event_bus.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>

namespace msl {

/// Playback engine with timeline sync, seek, and variable speed.
/// Mirrors Python SessionPlayer behavior.
class SessionPlayer {
public:
    explicit SessionPlayer(EventBus& bus) : event_bus_(bus) {}

    // --- Data callbacks (posted to EventBus) ---
    std::function<void(const CameraFrame&)>           on_camera_frame;
    std::function<void(const std::vector<float>&, int)> on_lidar_xyz;   // xyz data, num_points
    std::function<void(const std::vector<RadarTrack>&)> on_radar_tracks;
    std::function<void(const ImuData&)>                on_imu_data;
    std::function<void(const GpsFix&)>                 on_gps_data;
    std::function<void(int, double)>                   on_frame_changed; // frame_idx, time_sec
    std::function<void(bool)>                          on_state_changed; // playing

    /// Load a session for playback.
    bool loadSession(const std::string& session_dir);

    /// Set the reference sensor for the timeline.
    void setReferenceSensor(const std::string& sensor);

    /// Transport controls.
    void play();
    void pause();
    void seekToFrame(int frame);
    void nextFrame();
    void prevFrame();
    void setSpeed(float speed);

    /// Call each frame from the main loop to advance playback.
    void update(double current_time);

    /// Get session data (read-only).
    const SessionData& sessionData() const { return session_data_; }

    bool isPlaying() const { return playing_; }
    int  currentFrame() const { return current_frame_; }
    int  totalFrames() const { return static_cast<int>(ref_timestamps_.size()); }

private:
    EventBus& event_bus_;
    SessionData session_data_;

    // Timeline
    std::vector<double> ref_timestamps_;  // Reference sensor timeline
    std::string ref_sensor_ = "Lidar";
    int current_frame_ = 0;
    bool playing_ = false;
    float speed_ = 1.0f;
    double last_advance_time_ = 0.0;

    // Camera
    std::unique_ptr<CameraPrefetch> camera_prefetch_;
    int last_emitted_camera_ = -1;

    // Lidar
    std::unique_ptr<PcapReader> pcap_reader_;
    int last_emitted_lidar_ = -1;

    // Dedup
    int last_emitted_radar_ = -1;
    int last_emitted_gps_   = -1;
    int last_emitted_imu_   = -1;

    void buildReferenceTimeline();
    int findNearest(const std::vector<double>& timestamps, double target) const;
    void emitFrame(int frame_idx, bool force = false);
};

} // namespace msl
