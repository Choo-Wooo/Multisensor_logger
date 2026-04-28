#pragma once

#include <functional>
#include <string>
#include <vector>

namespace msl {

/// Left panel: playback controls (replaces SettingsPanel in player mode).
class PlayerPanel {
public:
    // Callbacks
    std::function<void(const std::string& path)> on_load_session;
    std::function<void()> on_play;
    std::function<void()> on_pause;
    std::function<void()> on_prev_frame;
    std::function<void()> on_next_frame;
    std::function<void(int frame)> on_seek;
    std::function<void(float speed)> on_speed_changed;
    std::function<void(const std::string& sensor)> on_reference_changed;
    /// Fired when the user picks a folder via Browse — host should persist this
    /// so the dialog reopens at the same place next time.
    std::function<void(const std::string& dir)> on_browse_dir_changed;

    /// @param default_data_dir   Fallback if last_browse_dir is empty
    /// @param last_browse_dir    Where the Browse dialog should reopen
    void render(const std::string& default_data_dir = "",
                const std::string& last_browse_dir = "");

    // Called by SessionPlayer to update display
    void setSessionInfo(const std::string& name, const std::vector<std::string>& sensors, int total_frames, double total_duration);
    void setCurrentFrame(int frame, double time_sec);
    void setPlaying(bool playing);

private:
    bool is_playing_ = false;
    int  current_frame_ = 0;
    int  total_frames_ = 0;
    double current_time_ = 0.0;
    double total_duration_ = 0.0;
    float current_speed_ = 1.0f;

    std::string session_name_;
    std::vector<std::string> available_sensors_;
    int ref_sensor_idx_ = 0;

    bool session_loaded_ = false;
    char path_buf_[512] = "";
};

} // namespace msl
