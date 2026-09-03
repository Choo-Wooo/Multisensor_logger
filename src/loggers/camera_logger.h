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
    /// 녹화 방식.
    ///  - Encode      : RGB/YUV 프레임을 받아 H.264 로 재인코딩 (USB 캠 등)
    ///  - Passthrough : 카메라가 보낸 H.264 비트스트림을 그대로 muxing.
    ///                  디코딩/인코딩이 전혀 없어 CPU 를 거의 쓰지 않고,
    ///                  세대 손실도 없다 (RTSP 기본 경로).
    enum class Mode { Encode, Passthrough };

    CameraLogger(const std::string& mov_path, int width, int height, int fps = 30,
                 Mode mode = Mode::Encode);
    ~CameraLogger();

    CameraLogger(const CameraLogger&) = delete;
    CameraLogger& operator=(const CameraLogger&) = delete;

    bool start();
    void writeFrame(const CameraFrame& frame);

    /// 압축 상태의 H.264 액세스 유닛을 그대로 기록 (Passthrough 전용).
    /// 네트워크 스레드에서 호출되므로 큐에 넣고 즉시 반환한다.
    void writeVideoPacket(CameraNalPacket&& pkt);

    /// 실제 스트림 해상도를 알려준다 (헤더 기록 전에만 반영된다).
    void setSourceSize(int width, int height);

    Mode mode() const { return mode_; }

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
    Mode mode_ = Mode::Encode;
    std::atomic<bool> running_{false};

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace msl
