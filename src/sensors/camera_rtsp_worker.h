#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace msl {

/// RTSP audio packet — passed straight through from live555 to MOV muxer.
/// Contains raw RTP payload (e.g. mu-law samples) + camera-side presentation time.
struct CameraAudioPacket {
    std::vector<uint8_t> data;      // RTP payload (e.g. mu-law samples)
    double pc_ts_rel = 0.0;         // seconds, relative to recording start
                                    // (derived from live555 presentationTime)
    int sample_rate = 8000;         // Hz
    int channels = 1;
    int codec_id = 0;               // AVCodecID, e.g. AV_CODEC_ID_PCM_MULAW
    int bits_per_sample = 8;
};

/// RTSP camera worker: live555 event loop + FFmpeg H.264 decoding.
/// Uses live555's presentationTime (RTCP-synced wall clock from camera) as
/// the source of PTS for video AND audio. This avoids decode/queue jitter
/// from corrupting timestamps.
class CameraRtspWorker : public ISensorWorker {
public:
    CameraRtspWorker(EventBus& bus, const std::string& url, int width, int height)
        : ISensorWorker(bus), url_(url), width_(width), height_(height) {}

    /// Video frame ready (decoded RGB).
    std::function<void(const CameraFrame&)> on_frame_ready;

    /// Audio packet ready (mu-law payload from RTSP, passthrough).
    std::function<void(const CameraAudioPacket&)> on_audio_packet;

    // live555 + FFmpeg internals (public for static response handlers in .cpp)
    struct Impl;
    Impl* impl_ = nullptr;

    /// Signal live555 event loop to exit.
    void signalEventLoopExit();

    /// Stop with live555 event loop signal.
    void stopRtsp() {
        if (impl_) signalEventLoopExit();
        stop();
    }

    /// Mark next received video/audio packet as the recording's time origin.
    /// Called from main thread when recording starts. The first packet's
    /// presentationTime becomes pc_ts_rel = 0.
    void resetRecordingClock() {
        rec_origin_us_.store(-1, std::memory_order_relaxed);
    }

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

    // -1 = not yet captured. First presentationTime received after this is
    // reset becomes the origin of pc_ts_rel.
    std::atomic<int64_t> rec_origin_us_{-1};

private:
    std::string url_;
    int width_;
    int height_;
};

} // namespace msl
