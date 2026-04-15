#pragma once

#include "core/sensor_data.h"
#include <string>
#include <vector>
#include <mutex>

namespace msl {

/// Simple sequential MP4 reader (like cv2.VideoCapture).
/// No background thread — decode on demand, keep position.
class CameraPrefetch {
public:
    CameraPrefetch() = default;
    ~CameraPrefetch();

    bool open(const std::string& mp4_path);
    void start() {}  // No-op (no background thread)
    void stop() {}
    void close();

    /// Get frame at index. If sequential, very fast. If jump, seeks.
    bool getFrame(int frame_idx, CameraFrame& out);

    /// Hint for prefetch direction (no-op in simple mode).
    void seekHint(int) {}

    int frameCount() const { return total_frames_; }
    const std::vector<double>& timestamps() const { return pts_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;

    std::string mp4_path_;
    int total_frames_ = 0;
    std::vector<double> pts_;
    int current_pos_ = -1;  // Last decoded frame index
    std::mutex decode_mutex_;

    bool decodeNextFrame(CameraFrame& out);
    bool seekAndDecode(int frame_idx, CameraFrame& out);
};

} // namespace msl
