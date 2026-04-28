#pragma once

#include "core/sensor_data.h"
#include "sensors/camera_rtsp_worker.h"
#include <string>
#include <atomic>
#include <cstdint>

namespace msl {

/// Camera logger: encodes RGB frames to H.264 MOV using FFmpeg C API.
/// Container is MOV (QuickTime), which supports both H.264 video and
/// pcm_mulaw (G.711) audio passthrough — common for IP-cam RTSP streams.
/// VFR (variable frame rate) with PTS based on pc_ts_rel (sourced from
/// live555 presentationTime, not local wall-clock).
class CameraLogger {
public:
    CameraLogger(const std::string& mov_path, int width, int height, int fps = 30);
    ~CameraLogger();

    CameraLogger(const CameraLogger&) = delete;
    CameraLogger& operator=(const CameraLogger&) = delete;

    bool start();
    void writeFrame(const CameraFrame& frame);

    /// Append an audio packet (e.g. mu-law from RTSP) — passthrough, no re-encode.
    /// Safe to call from a different thread than writeFrame() (mutex-protected).
    void writeAudioPacket(const CameraAudioPacket& pkt);

    void stop();
    bool isRunning() const { return running_; }

private:
    std::string mov_path_;
    int width_;
    int height_;
    int fps_;
    std::atomic<bool> running_{false};

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace msl
