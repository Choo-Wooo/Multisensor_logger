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

/// 기록 타임스탬프의 기준 상태 (UI 표시용).
enum class CameraTimeSource {
    Unknown = 0,   ///< 아직 판단 전 (RTCP Sender Report 대기)
    CameraNtp,     ///< 카메라 시각(RTCP/NTP) 신뢰 가능 — 스트림 지연 보정 가능
    PcClock        ///< 카메라 시각 사용 불가 — 도착 시각(PC 시계) 기준
};

/// RTSP camera worker: live555 event loop + FFmpeg H.264 decoding.
/// Uses live555's presentationTime (RTCP-synced wall clock from camera) as
/// the source of PTS for video AND audio. This avoids decode/queue jitter
/// from corrupting timestamps. Camera timestamps are sanity-clamped against
/// the PC clock (see Impl::guardedPcTsRel) so cameras with a broken RTP clock
/// rate cannot stretch/compress the recorded timeline.
class CameraRtspWorker : public ISensorWorker {
public:
    CameraRtspWorker(EventBus& bus, const std::string& url, int width, int height)
        : ISensorWorker(bus), url_(url), width_(width), height_(height) {}

    /// Video frame ready (decoded RGB, 프리뷰 크기).
    std::function<void(const CameraFrame&)> on_frame_ready;

    /// 압축 상태 그대로의 H.264 액세스 유닛 (녹화용 패스스루).
    /// 설정되어 있으면 녹화 경로는 디코딩/인코딩을 전혀 거치지 않는다.
    /// 네트워크 스레드에서 호출되므로 구현은 큐잉만 하고 즉시 반환해야 한다.
    std::function<void(CameraNalPacket&&)> on_video_nal;

    /// 현재 판단된 시간 기준 (UI 표시용). 연결 직후부터 갱신된다.
    CameraTimeSource timeSource() const {
        return static_cast<CameraTimeSource>(time_source_state_.load(std::memory_order_relaxed));
    }
    /// 카메라 시각과 PC 시각의 차이(초). Unknown 이면 의미 없음.
    double clockDiffSec() const { return clock_diff_sec_.load(std::memory_order_relaxed); }

    /// 시간 기준을 PC 시계로 고정할지 여부.
    /// true 면 RTCP/카메라 시각을 아예 쓰지 않고 도착 시각만 쓴다
    /// (config: time_source = pc). 카메라 NTP 를 신뢰할 수 없을 때 사용.
    void setPcClockOnly(bool pc_only) {
        time_source_pc_only_.store(pc_only, std::memory_order_relaxed);
    }

    /// 이 스트림의 고정 지연 보정(초). 기록되는 pc_ts_rel 에서 빼진다.
    void setTimeOffset(double seconds) {
        time_offset_sec_.store(seconds, std::memory_order_relaxed);
    }

    /// 프리뷰 프레임을 원본 해상도로 받을지 여부.
    /// 패스스루 녹화(on_video_nal 사용) 시에는 false 로 두어 축소 변환만 한다.
    void setFullFrameOutput(bool full) {
        full_frame_output_.store(full, std::memory_order_relaxed);
    }

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
    /// presentationTime is pinned to the PC-clock offset at which it arrived,
    /// so every channel shares the same zero point (= recording start).
    void resetRecordingClock() {
        rec_origin_us_.store(-1, std::memory_order_relaxed);
        rec_origin_local_rel_.store(0.0, std::memory_order_relaxed);
        rec_epoch_.fetch_add(1, std::memory_order_relaxed);
    }

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

    // -1 = not yet captured. First presentationTime received after this is
    // reset becomes the origin of pc_ts_rel.
    std::atomic<int64_t> rec_origin_us_{-1};

    // PC-clock offset (seconds since recording start) at which that first
    // packet arrived. pc_ts_rel = (pt - rec_origin_us_) + rec_origin_local_rel_,
    // i.e. the camera clock supplies the *pacing* and the PC clock supplies the
    // *anchor*. Without this every channel would start at 0 no matter when its
    // first frame actually arrived, offsetting the channels against each other
    // (measured ~200 ms with two 4K cameras) and against lidar/radar.
    std::atomic<double> rec_origin_local_rel_{0.0};

    // Bumped on every resetRecordingClock(); lets per-stream timestamp guards
    // (in Impl) detect a new recording session and reset their state.
    std::atomic<uint32_t> rec_epoch_{0};

    // 프리뷰를 원본 해상도로 만들지 여부 (기본: 축소).
    std::atomic<bool> full_frame_output_{false};

    // 고정 지연 보정 (초). 기록 PTS 에서 뺀다.
    std::atomic<double> time_offset_sec_{0.0};

    // RTCP 로 측정한 이 세션의 스트림 지연(초). 녹화 시작 직후 측정 창에서 정해진다.
    std::atomic<double> stream_latency_sec_{0.0};

    // true = 카메라 시각을 아예 쓰지 않고 PC 시계만 사용
    std::atomic<bool> time_source_pc_only_{false};

    // UI 표시용 상태 (CameraTimeSource)
    std::atomic<int>    time_source_state_{0};
    std::atomic<double> clock_diff_sec_{0.0};

private:
    std::string url_;
    int width_;
    int height_;
};

} // namespace msl
