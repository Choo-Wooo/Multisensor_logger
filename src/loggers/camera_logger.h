#pragma once

#include "core/sensor_data.h"
#include <string>
#include <atomic>
#include <cstdint>

namespace msl {

/// Camera logger: encodes RGB frames to H.264 MP4 using FFmpeg C API.
/// Uses avformat muxer for proper MP4 container + libx264 encoder.
/// VFR (variable frame rate) with PTS based on pc_ts_rel.
class CameraLogger {
public:
    CameraLogger(const std::string& mp4_path, int width, int height, int fps = 30);
    ~CameraLogger();

    CameraLogger(const CameraLogger&) = delete;
    CameraLogger& operator=(const CameraLogger&) = delete;

    bool start();
    void writeFrame(const CameraFrame& frame);
    void stop();
    bool isRunning() const { return running_; }

private:
    std::string mp4_path_;
    int width_;
    int height_;
    int fps_;
    std::atomic<bool> running_{false};

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace msl
