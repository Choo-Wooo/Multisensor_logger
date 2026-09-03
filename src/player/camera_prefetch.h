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

    /// 다음 프레임을 디코딩한다. want_output=false 면 화면용 변환/복사를 생략한다.
    bool decodeNextFrame(CameraFrame& out, bool want_output = true);

    /// 이 프레임 수 이내로 앞서 나가는 요청은 시크 없이 순차 디코딩으로 따라간다.
    static constexpr int kMaxForwardDecode = 16;
    bool seekAndDecode(int frame_idx, CameraFrame& out);
};

} // namespace msl
