#include "camera_rtsp_worker.h"
#include "core/logging.h"
#include "core/clock.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <deque>
#include <utility>
#include <vector>

// FFmpeg
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// live555
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>

namespace msl {

// Forward declarations
static void onDescribeResponse(RTSPClient*, int, char*);
static void onSetupResponse(RTSPClient*, int, char*);
static void onPlayResponse(RTSPClient*, int, char*);

// Helper: parse username:password from rtsp://user:pass@host/...
static bool parseRtspCredentials(const std::string& url, std::string& user, std::string& pass) {
    auto proto_end = url.find("://");
    if (proto_end == std::string::npos) return false;
    auto auth_start = proto_end + 3;
    auto at_pos = url.find('@', auth_start);
    if (at_pos == std::string::npos) return false;
    auto colon_pos = url.find(':', auth_start);
    if (colon_pos == std::string::npos || colon_pos > at_pos) return false;
    user = url.substr(auth_start, colon_pos - auth_start);
    pass = url.substr(colon_pos + 1, at_pos - colon_pos - 1);
    return true;
}

// timeval → microseconds
static inline int64_t tv_to_us(struct timeval tv) {
    return static_cast<int64_t>(tv.tv_sec) * 1000000LL + tv.tv_usec;
}

// Portable case-insensitive ASCII string compare (Windows lacks iequals).
static int iequals(const char* a, const char* b) {
    if (!a || !b) return -1;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return (int)ca - (int)cb;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// ============================================================================
// PIMPL
// ============================================================================
struct CameraRtspWorker::Impl {
    // live555
    TaskScheduler* scheduler = nullptr;
    UsageEnvironment* env = nullptr;
    RTSPClient* rtspClient = nullptr;
    MediaSession* session = nullptr;
    MediaSubsession* videoSub = nullptr;
    MediaSubsession* audioSub = nullptr;
    EventLoopWatchVariable watchVariable{0};
    std::thread eventLoopThread;
    Authenticator* auth = nullptr;  // Digest/Basic authentication

    // Subsessions remaining to set up (we walk through them sequentially)
    MediaSubsessionIterator* setupIter = nullptr;

    // Audio source info (from SDP)
    int audio_sample_rate = 8000;
    int audio_channels = 1;
    int audio_codec_id = 0;     // AVCodecID
    int audio_bits_per_sample = 8;

    // FFmpeg decoder (video only)
    const AVCodec* codec = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgbFrame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* swsCtx = nullptr;
    uint8_t* rgbBuffer = nullptr;
    std::vector<uint8_t> annexbBuf;  // 현재 액세스 유닛(Annex-B) 누적 버퍼
    int64_t auPtsUs = 0;             // 누적 중인 액세스 유닛의 presentationTime(us)
    double  auArrival = 0.0;         // 그 유닛의 첫 NAL 도착 시각
    bool    auHasData = false;       // 누적 중인 NAL 이 있는지
    bool    auIsKeyframe = false;    // SPS/PPS/IDR 포함 여부

    // 프리뷰 스케일러 (원본 -> kPreviewMaxWidth 이하 I420)
    int previewWidth = 0;
    int previewHeight = 0;
    bool scaleNeeded = true;   // 원본이 이미 I420 & 축소 불필요면 false
    bool swsReady = false;
    bool fullRange = false;    // yuvj420p 여부

    int width = 0;
    int height = 0;
    bool codecOpened = false;
    CameraRtspWorker* owner = nullptr;

    // --- Async decode pipeline ---
    // The live555 network callback must NOT block on decode + 4K YUV->RGB
    // conversion (~24MB/frame): doing so stalls socket reads, the server then
    // drops packets ("reader is too slow") and frames decode corrupt (green).
    // Instead the callback just copies each NAL into this queue, and a worker
    // thread does the heavy decode/convert.
    struct PendingNal {
        std::vector<uint8_t> data;   // AnnexB 액세스 유닛 전체
        int64_t pt_us = 0;           // presentationTime (µs)
        double arrival_wall = 0.0;   // Clock::now() at network arrival
    };
    std::queue<PendingNal> nalQueue;
    std::mutex nalMutex;
    std::condition_variable nalCv;
    std::thread decodeThread;
    std::atomic<bool> decodeRunning{false};
    unsigned dropped_nals_ = 0;
    // 액세스 유닛(=프레임) 단위 큐. 프리뷰 전용이라 깊게 잡을 필요가 없다.
    static constexpr size_t kMaxQueuedAccessUnits = 24;   // ~1.6s @15fps

    // 프리뷰 RGB 최대 가로 크기. 4K 원본을 그대로 RGB 로 바꾸면 프레임당
    // 24MB 복사 + 텍스처 업로드가 발생하는데, 화면 패널은 그보다 훨씬 작다.
    static constexpr int kPreviewMaxWidth = 1280;

    // After an overflow flush, skip non-decodable NALs until the next IDR
    // (guarded by nalMutex). Decoding P-frames whose references were dropped
    // produces smeared/green output until the next keyframe.
    bool waitingForIdr = false;
    double waitingForIdrSince = 0.0;

    // Safety net: never destroy Impl with a joinable decode thread (would call
    // std::terminate). closeDecoder() normally stops it first; this covers
    // early-return error paths in onConnect().
    ~Impl() { stopDecodeThread(); }

    // --- Timestamp sanity guard ---------------------------------------------
    // Some cameras advance their RTP clock at the wrong rate (e.g. RTP ticks
    // spaced for half the actual frame rate → recorded video plays 2x slow),
    // and presentationTime can also jump when the first RTCP sender report
    // remaps it onto the camera's NTP clock. Both corrupt recorded PTS while
    // live view (which ignores timestamps) looks fine. Guard: the
    // presentationTime-relative timeline may never drift more than
    // kMaxTsDriftSec from the PC-clock timeline shared by all other sensors;
    // outside that window the PC clock wins. Within the window the camera
    // clock is used as-is, keeping its jitter-free frame pacing.
    static constexpr double kMaxTsDriftSec = 0.2;

    // 앵커 보정 속도 (프레임당 최대 이동량). 15fps 기준 15 ms/s 이므로
    // 200 ms 오차를 ~13 초에 흡수하면서도 프레임 간격 왜곡은 1 ms 이하다.
    static constexpr double kAnchorSlewSec = 0.001;

    struct TsGuard {
        const char* name;
        uint32_t epoch = 0xFFFFFFFFu;
        double last_out = -1.0;
        uint64_t clamped = 0;
    };
    TsGuard videoGuard{"video"};   // used only from the decode thread
    TsGuard audioGuard{"audio"};   // used only from the live555 network thread
    TsGuard recordGuard{"record"}; // 패스스루 녹화 (network thread)

    // Maps packet pts (presentationTime µs) → local arrival wall time across
    // the decoder, which may buffer frames. Decode thread only.
    // NOTE: must NOT ride in pkt->opaque — the pointer is 4 bytes on the
    // 32-bit build and silently truncates the 64-bit timestamp.
    std::deque<std::pair<int64_t, double>> pts_arrivals_;

    // Compute pc_ts_rel from a presentationTime (µs) + local arrival wall time.
    // Captures the first call after a resetRecordingClock() as the origin.
    // RTCP Sender Report 로 카메라 시각이 PC 시각(Unix epoch)에 매핑되었는지.
    // 매핑되었다면 presentationTime 은 "촬영 시각" 이므로 전송/버퍼 지연을 빼고
    // 라이다/레이더와 같은 절대 시간축에 올릴 수 있다.
    bool rtcpSynced() const {
        return videoSub && videoSub->rtpSource() &&
               videoSub->rtpSource()->hasBeenSynchronizedUsingRTCP();
    }
    bool rtcp_logged = false;
    uint32_t late_check_ = 0;

    // --- 스트림 지연 측정 상태 ---
    // 물리적으로 가능한 RTSP 지연 범위. 이 밖의 값은 카메라 시계가 틀어진 것으로
    // 보고 무시한다 (예: NTP 미설정으로 몇 초/몇 년씩 어긋난 경우).
    static constexpr double kMinPlausibleLatency = -0.050;  // -50 ms (약간의 시계 오차 허용)
    static constexpr double kMaxPlausibleLatency =  1.000;  // +1 s
    static constexpr uint32_t kLatencyWindowPackets = 45;   // ~3초 @15fps
    static constexpr uint32_t kMinLatencySamples = 10;
    uint32_t ts_state_check_ = 0;
    double   lat_min_ = 1e9;
    uint32_t lat_samples_ = 0;
    uint32_t lat_window_ = 0;
    bool     lat_locked_ = false;

    double guardedPcTsRel(int64_t pt_us, double arrival_wall, TsGuard& g) {
        uint32_t epoch = owner->rec_epoch_.load(std::memory_order_relaxed);
        if (g.epoch != epoch) {
            // New recording session — reset per-stream guard state.
            g.epoch = epoch;
            g.last_out = -1.0;
            g.clamped = 0;
        }

        double local_rel = arrival_wall - owner->rec_start_time_.load();
        const double user_offset = owner->time_offset_sec_.load(std::memory_order_relaxed);

        int64_t origin = owner->rec_origin_us_.load(std::memory_order_relaxed);
        if (origin < 0) {
            // 녹화 시작 후 첫 패킷 — 무조건 "도착 시각(PC 시계)" 기준으로 시작한다.
            // 카메라 NTP 가 설정되어 있지 않거나 RTCP 가 늦게 오는 경우가 흔하므로,
            // 기본 동작은 항상 다른 센서(라이다/레이더)와 같은 PC 시계다.
            // 카메라 시각을 믿을 수 있다고 "확인된" 경우에만 아래 측정 창에서
            // 지연 보정값을 서서히 반영한다.
            owner->rec_origin_us_.store(pt_us, std::memory_order_relaxed);
            owner->rec_origin_local_rel_.store(local_rel > 0.0 ? local_rel : 0.0,
                                               std::memory_order_relaxed);
            owner->stream_latency_sec_.store(0.0, std::memory_order_relaxed);
            lat_samples_ = 0;
            lat_min_ = 1e9;
            lat_locked_ = false;
            origin = pt_us;
        }

        // --- 스트림 지연 측정 (녹화 스트림에서만, 시작 후 약 3초 동안) ---
        // RTCP Sender Report 로 카메라 시각이 Unix epoch 에 매핑되어 있을 때만
        // 의미가 있다. 측정값은 (도착 - 촬영) = 인코딩+네트워크+버퍼 지연이며,
        // 지터의 영향을 줄이기 위해 최솟값을 쓴다. 값이 물리적으로 말이 되는
        // 범위를 벗어나면(= 카메라 시계가 틀어짐) 그냥 버리고 PC 시계를 유지한다.
        if (&g == &recordGuard && !lat_locked_ && !owner->time_source_pc_only_.load(std::memory_order_relaxed)) {
            if (rtcpSynced()) {
                double abs_rel = pt_us * 1e-6 - owner->rec_start_time_.load();
                double diff = local_rel - abs_rel;
                if (diff > kMinPlausibleLatency && diff < kMaxPlausibleLatency) {
                    if (diff < lat_min_) lat_min_ = diff;
                    ++lat_samples_;
                } else {
                    // 카메라 시계가 신뢰 범위 밖 — 이번 세션은 PC 시계로 고정
                    lat_locked_ = true;
                    if (!rtcp_logged) {
                        rtcp_logged = true;
                        owner->L()->warn("RTSP {}: 카메라 시각이 PC 와 {:.1f}s 차이 — NTP 미설정으로 "
                                     "보입니다. 도착 시각(PC 시계) 기준으로 기록합니다.",
                                     g.name, -diff);
                    }
                }
            }
            if (!lat_locked_ && ++lat_window_ > kLatencyWindowPackets) {
                lat_locked_ = true;
                if (lat_samples_ >= kMinLatencySamples) {
                    owner->stream_latency_sec_.store(lat_min_, std::memory_order_relaxed);
                    owner->L()->info("RTSP {}: RTCP 동기 확인 — 스트림 지연 {:.0f} ms 보정 "
                                 "(샘플 {}개, 앵커가 서서히 이동)",
                                 g.name, lat_min_ * 1000.0, lat_samples_);
                } else {
                    owner->L()->info("RTSP {}: RTCP 시각 없음/불충분 — 도착 시각(PC 시계) 기준으로 "
                                 "기록합니다", g.name);
                }
            }
        }

        // 지연 보정된 PC 시간축 (라이다/레이더와 같은 기준)
        const double latency = owner->stream_latency_sec_.load(std::memory_order_relaxed);
        const double ref_rel = local_rel - latency;

        // 카메라 시계는 (a) 클럭 레이트가 PC 와 미세하게 다르고 (b) RTCP 재동기 시
        // 점프할 수 있다. 앵커를 고정해 두면 그 오차가 그대로 남으므로, 프레임당
        // 최대 kAnchorSlewSec 만큼만 기준 시간축 쪽으로 천천히 끌어당긴다(느린 PLL).
        double anchor = owner->rec_origin_local_rel_.load(std::memory_order_relaxed);
        double pt_raw = (pt_us - origin) * 1e-6;
        double err = (ref_rel - pt_raw) - anchor;
        if (err >  kAnchorSlewSec) err =  kAnchorSlewSec;
        if (err < -kAnchorSlewSec) err = -kAnchorSlewSec;
        anchor += err;
        owner->rec_origin_local_rel_.store(anchor, std::memory_order_relaxed);

        double pt_rel = pt_raw + anchor;

        double lo = ref_rel - kMaxTsDriftSec;
        double hi = ref_rel + kMaxTsDriftSec;
        double out = pt_rel;
        if (out < lo || out > hi) {
            out = (out < lo) ? lo : hi;
            if ((g.clamped++ % 150) == 0) {
                owner->L()->warn("RTSP {}: camera timestamps drifting from real time "
                             "(camera={:.3f}s vs pc={:.3f}s, rate~{:.2f}x) — "
                             "clamping recorded PTS to PC clock",
                             g.name, pt_rel, ref_rel,
                             ref_rel > 1.0 ? pt_rel / ref_rel : 0.0);
            }
        }

        // 사용자 지정 고정 지연 보정 (config: time_offset_ms)
        out -= user_offset;

        // The muxer requires monotonically increasing PTS.
        if (out < 0.0) out = 0.0;
        if (out <= g.last_out) out = g.last_out + 0.0005;
        g.last_out = out;
        return out;
    }

    // Public-friendly wrapper — checks recording_ (which Impl can access as
    // a nested type of CameraRtspWorker) and only then computes pc_ts_rel.
    // Used by AudioFrameSink (which is a separate class without privileged access).
    bool tryComputeRecordingPcTsRel(struct timeval pt, double& out_pc_ts_rel) {
        if (!owner->recording_) return false;
        out_pc_ts_rel = guardedPcTsRel(tv_to_us(pt), Clock::now(), audioGuard);
        return true;
    }

    // --- Decoder ---

    bool openDecoder() {
        // Suppress "no frame!" — it's harmless (SPS/PPS/SEI NALs produce no output)
        // Only forward FATAL (AV_LOG_PANIC=0, AV_LOG_FATAL=8)
        av_log_set_level(AV_LOG_FATAL);
        av_log_set_callback([](void*, int level, const char* fmt, va_list vl) {
            if (level > AV_LOG_FATAL) return;
            char buf[1024];
            vsnprintf(buf, sizeof(buf), fmt, vl);
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
            msl::log::camera(0)->error("[FFmpeg] {}", buf);
        });

        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) return false;

        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) return false;

        // Low-latency flags for real-time streaming
        codecCtx->flags  |= AV_CODEC_FLAG_LOW_DELAY;
        codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

        if (owner->width_ >= 3840 || owner->height_ >= 2160) {
            codecCtx->thread_count = 4;
        }else if (owner->width_ >= 1920 || owner->height_ >= 1080) {
            codecCtx->thread_count = 2;
        }else {
            codecCtx->thread_count = 1;
        }

        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&codecCtx);
            return false;
        }

        frame = av_frame_alloc();
        rgbFrame = av_frame_alloc();
        pkt = av_packet_alloc();
        codecOpened = true;
        if (!(frame && rgbFrame && pkt)) return false;

        // Start the worker that drains nalQueue (decode + RGB convert).
        startDecodeThread();
        return true;
    }

    // Decode H.264 NAL with associated presentationTime.
    // The packet's pts is set to presentationTime (microseconds) so the
    // decoder propagates it to the output AVFrame; the local arrival wall
    // time rides along in pkt->opaque (COPY_OPAQUE) for the timestamp guard.
    // 네트워크 스레드에서 이미 액세스 유닛(AnnexB) 단위로 모아 넘겨준 버퍼를
    // 한 번에 디코딩한다. (4K 멀티슬라이스 스트림은 한 프레임이 여러 NAL 로
    // 나뉘어 오는데, NAL 하나씩 디코더에 넣으면 부분 픽처가 프레임 수만큼
    // 쏟아진다.)
    void decodeAccessUnit(uint8_t* data, size_t size, int64_t pt_us,
                          double arrival_wall) {
        if (!codecOpened || !data || size == 0) return;

        if (pts_arrivals_.empty() || pts_arrivals_.back().first != pt_us) {
            pts_arrivals_.emplace_back(pt_us, arrival_wall);
            if (pts_arrivals_.size() > 64) pts_arrivals_.pop_front();
        }

        pkt->data = data;
        pkt->size = static_cast<int>(size);
        pkt->pts  = pt_us;   // microseconds — survives through decoder
        pkt->dts  = pkt->pts;

        int ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) return;

        while (true) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            deliverFrame();
            av_frame_unref(frame);
        }
    }

    void deliverFrame() {
        if (!frame || frame->width <= 0 || frame->height <= 0) return;

        // 첫 프레임에서 스케일러 구성.
        // 출력은 RGB 가 아니라 I420(디코더 출력과 같은 포맷)이다. 색공간 변환은
        // 화면 표시 시 GPU 셰이더가 하고, 재인코딩 폴백에서는 인코더가 그대로
        // 받는다 — CPU 색공간 변환이 파이프라인에서 완전히 사라진다.
        if (!swsReady) {
            width = frame->width;
            height = frame->height;

            previewWidth = width;
            previewHeight = height;
            if (!owner->full_frame_output_.load(std::memory_order_relaxed) &&
                width > kPreviewMaxWidth) {
                previewWidth = kPreviewMaxWidth & ~1;
                previewHeight = (int)((int64_t)height * previewWidth / width) & ~1;
                if (previewHeight <= 0) previewHeight = height;
            }

            AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
            // yuvj*(풀레인지)는 동일 크로마 포맷의 일반 표기로만 바꿔 준다.
            fullRange = (srcFmt == AV_PIX_FMT_YUVJ420P ||
                         srcFmt == AV_PIX_FMT_YUVJ422P ||
                         srcFmt == AV_PIX_FMT_YUVJ444P);
            if (srcFmt == AV_PIX_FMT_YUVJ420P) srcFmt = AV_PIX_FMT_YUV420P;
            if (srcFmt == AV_PIX_FMT_YUVJ422P) srcFmt = AV_PIX_FMT_YUV422P;
            if (srcFmt == AV_PIX_FMT_YUVJ444P) srcFmt = AV_PIX_FMT_YUV444P;

            const bool passthrough_scale =
                (srcFmt == AV_PIX_FMT_YUV420P && previewWidth == width &&
                 previewHeight == height);
            if (!passthrough_scale) {
                swsCtx = sws_getContext(width, height, srcFmt,
                                        previewWidth, previewHeight, AV_PIX_FMT_YUV420P,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                int bufSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, previewWidth,
                                                       previewHeight, 1);
                rgbBuffer = static_cast<uint8_t*>(av_malloc(bufSize));
                av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                                     rgbBuffer, AV_PIX_FMT_YUV420P, previewWidth,
                                     previewHeight, 1);
            }
            scaleNeeded = !passthrough_scale;
            swsReady = true;

            owner->L()->info("RTSP: Decoded frame {}x{} (preview {}x{}, I420 {})",
                         width, height, previewWidth, previewHeight,
                         scaleNeeded ? "scaled" : "direct");
        }

        if (scaleNeeded && swsCtx) {
            sws_scale(swsCtx, frame->data, frame->linesize, 0, height,
                      rgbFrame->data, rgbFrame->linesize);
        }

        if (owner && owner->on_frame_ready) {
            CameraFrame cf;
            cf.width = previewWidth;
            cf.height = previewHeight;
            cf.src_width = width;
            cf.src_height = height;
            cf.yuv_full_range = fullRange;

            const size_t ySize  = (size_t)previewWidth * previewHeight;
            const size_t uvSize = (size_t)(previewWidth / 2) * (previewHeight / 2);
            cf.yuv_data.resize(ySize + uvSize * 2);
            // 스케일 없이 바로 전달할 때는 디코더 프레임에서, 아니면 스케일 결과에서 복사
            uint8_t* const* srcData = scaleNeeded ? rgbFrame->data : frame->data;
            const int* srcLine = scaleNeeded ? rgbFrame->linesize : frame->linesize;
            uint8_t* dst[3] = { cf.yuv_data.data(),
                                cf.yuv_data.data() + ySize,
                                cf.yuv_data.data() + ySize + uvSize };
            const int dstLine[3] = { previewWidth, previewWidth / 2, previewWidth / 2 };
            for (int p = 0; p < 3; ++p) {
                const int h = (p == 0) ? previewHeight : previewHeight / 2;
                const int bytes = dstLine[p];
                for (int y = 0; y < h; ++y)
                    std::memcpy(dst[p] + (size_t)y * bytes,
                                srcData[p] + (size_t)y * srcLine[p], bytes);
            }

            // 패스스루 녹화(on_video_nal)를 쓰는 경우 PTS 는 네트워크 스레드에서
            // 이미 계산했다. 여기서 또 계산하면 공용 앵커가 두 번 보정되므로
            // 재인코딩 폴백일 때만 계산한다.
            if (owner->recording_ && !owner->on_video_nal) {
                double arrival_wall = 0.0;
                while (!pts_arrivals_.empty() &&
                       pts_arrivals_.front().first < frame->pts) {
                    pts_arrivals_.pop_front();
                }
                if (!pts_arrivals_.empty() &&
                    pts_arrivals_.front().first == frame->pts) {
                    arrival_wall = pts_arrivals_.front().second;
                    pts_arrivals_.pop_front();
                }
                if (arrival_wall <= 0.0) arrival_wall = Clock::now();
                cf.pc_ts_rel = guardedPcTsRel(frame->pts, arrival_wall, videoGuard);
            }
            owner->frame_count_++;

            owner->on_frame_ready(cf);
        }
    }

    // --- Async decode pipeline methods ---

    // Called from the live555 network thread: copy the NAL and return fast.
    // live555 네트워크 스레드에서 호출된다. 같은 presentationTime 을 갖는 NAL 을
    // 하나의 액세스 유닛으로 모으고, 유닛이 끝나면
    //   (1) 녹화(패스스루) 경로로 압축 상태 그대로 넘기고
    //   (2) 프리뷰 디코드 큐에 넣는다.
    // 녹화가 (1)에서 끝나므로, 디코더가 밀려서 큐가 넘쳐도 파일에는 영향이 없다.
    void enqueueNal(const uint8_t* data, unsigned size, struct timeval pt) {
        if (!data || size == 0) return;
        const double arrival = Clock::now();
        const int64_t pt_us = tv_to_us(pt);
        const uint8_t nalType = data[0] & 0x1F;

        if (auHasData && pt_us != auPtsUs) completeAccessUnit();

        if (!auHasData) {
            auPtsUs = pt_us;
            auArrival = arrival;
            auIsKeyframe = false;
            annexbBuf.clear();
        }
        static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        const size_t off = annexbBuf.size();
        annexbBuf.resize(off + 4 + size);
        std::memcpy(annexbBuf.data() + off, kStartCode, 4);
        std::memcpy(annexbBuf.data() + off + 4, data, size);
        auHasData = true;
        // SPS(7)/PPS(8)/IDR(5) 중 하나라도 있으면 키프레임 유닛으로 본다.
        if (nalType == 5 || nalType == 7 || nalType == 8) auIsKeyframe = true;
    }

    // 카메라 시각(RTCP/NTP)을 믿을 수 있는지 주기적으로 판정한다 (네트워크 스레드).
    // 녹화 중이 아니어도 갱신되므로, 연결만 해두면 UI 에서 바로 확인할 수 있다.
    void updateTimeSourceState(int64_t pt_us, double arrival_wall) {
        if (owner->time_source_pc_only_.load(std::memory_order_relaxed)) {
            owner->time_source_state_.store((int)CameraTimeSource::PcClock,
                                            std::memory_order_relaxed);
            return;
        }
        if ((++ts_state_check_ % 15) != 0) return;   // ~1초에 한 번
        if (!rtcpSynced()) {
            owner->time_source_state_.store((int)CameraTimeSource::Unknown,
                                            std::memory_order_relaxed);
            return;
        }
        // presentationTime 이 Unix epoch 로 매핑된 상태 — PC 시각과 비교한다.
        const double diff = arrival_wall - pt_us * 1e-6;   // >0 : 도착이 늦음(지연)
        owner->clock_diff_sec_.store(diff, std::memory_order_relaxed);
        const bool ok = (diff > kMinPlausibleLatency && diff < kMaxPlausibleLatency);
        owner->time_source_state_.store(
            (int)(ok ? CameraTimeSource::CameraNtp : CameraTimeSource::PcClock),
            std::memory_order_relaxed);
    }

    // 액세스 유닛 하나를 확정한다 (네트워크 스레드).
    void completeAccessUnit() {
        if (!auHasData || annexbBuf.empty()) { auHasData = false; return; }

        updateTimeSourceState(auPtsUs, auArrival);

        // (1) 녹화: 압축 그대로. 재인코딩/디코딩 없음.
        if (owner->on_video_nal && owner->recording_) {
            CameraNalPacket np;
            np.data.assign(annexbBuf.begin(), annexbBuf.end());
            np.keyframe = auIsKeyframe;
            np.pc_ts_rel = guardedPcTsRel(auPtsUs, auArrival, recordGuard);
            owner->on_video_nal(std::move(np));
        }

        // (2) 프리뷰 디코드 큐
        {
            std::lock_guard<std::mutex> lk(nalMutex);
            if (waitingForIdr) {
                if (auIsKeyframe) {
                    waitingForIdr = false;
                } else if (auArrival - waitingForIdrSince > 5.0) {
                    waitingForIdr = false;
                    owner->L()->warn("RTSP: no keyframe within 5s after preview "
                                 "queue flush — resuming decode anyway");
                } else {
                    annexbBuf.clear();
                    auHasData = false;
                    return;   // 참조 프레임이 없어 디코딩 불가
                }
            }

            if (nalQueue.size() >= kMaxQueuedAccessUnits) {
                // 프리뷰 디코더가 스트림을 못 따라간다. 임의로 버리면 다음
                // 키프레임까지 화면이 깨지므로 밀린 것을 통째로 비우고 다음
                // 키프레임부터 다시 디코딩한다. (녹화 파일에는 영향 없음)
                size_t flushed = nalQueue.size();
                std::queue<PendingNal> empty;
                std::swap(nalQueue, empty);
                if (!auIsKeyframe) {
                    waitingForIdr = true;
                    waitingForIdrSince = auArrival;
                }
                if ((dropped_nals_++ % 30) == 0) {
                    owner->L()->warn("RTSP: preview decode queue full — flushed {} "
                                 "access units (recording is unaffected)", flushed);
                }
                if (!auIsKeyframe) {
                    annexbBuf.clear();
                    auHasData = false;
                    return;
                }
            }
            nalQueue.emplace();
            PendingNal& slot = nalQueue.back();
            slot.data.swap(annexbBuf);      // 버퍼 소유권 이동 (복사 없음)
            slot.pt_us = auPtsUs;
            slot.arrival_wall = auArrival;
        }
        nalCv.notify_one();
        annexbBuf.clear();
        auHasData = false;
    }

    void decodeLoop() {
        for (;;) {
            PendingNal item;
            {
                std::unique_lock<std::mutex> lk(nalMutex);
                nalCv.wait(lk, [this] {
                    return !decodeRunning.load() || !nalQueue.empty();
                });
                if (!decodeRunning.load()) break;
                item = std::move(nalQueue.front());
                nalQueue.pop();
            }
            decodeAccessUnit(item.data.data(), item.data.size(), item.pt_us,
                             item.arrival_wall);
        }
    }

    void startDecodeThread() {
        if (decodeRunning.load()) return;
        decodeRunning.store(true);
        decodeThread = std::thread([this] { decodeLoop(); });
    }

    void stopDecodeThread() {
        if (!decodeRunning.load() && !decodeThread.joinable()) return;
        decodeRunning.store(false);
        nalCv.notify_all();
        if (decodeThread.joinable()) decodeThread.join();
        // Drop anything still queued.
        std::queue<PendingNal> empty;
        std::lock_guard<std::mutex> lk(nalMutex);
        std::swap(nalQueue, empty);
        waitingForIdr = false;
    }

    void closeDecoder() {
        // Stop the worker BEFORE freeing decoder resources it uses.
        stopDecodeThread();
        // 남아 있는 액세스 유닛은 버린다 (디코드 스레드는 이미 정지 상태)
        annexbBuf.clear();
        auHasData = false;
        if (swsCtx)   { sws_freeContext(swsCtx); swsCtx = nullptr; }
        if (rgbBuffer) { av_free(rgbBuffer); rgbBuffer = nullptr; }
        if (pkt)       { av_packet_free(&pkt); }
        if (rgbFrame)  { av_frame_free(&rgbFrame); }
        if (frame)     { av_frame_free(&frame); }
        if (codecCtx)  { avcodec_free_context(&codecCtx); }
        codecOpened = false;
        width = height = 0;
    }

    void shutdownStream() {
        // 1. Stop sinks
        if (videoSub && videoSub->sink) {
            videoSub->sink->stopPlaying();
            Medium::close(videoSub->sink);
            videoSub->sink = nullptr;
        }
        if (audioSub && audioSub->sink) {
            audioSub->sink->stopPlaying();
            Medium::close(audioSub->sink);
            audioSub->sink = nullptr;
        }

        // 2. De-initiate subsessions
        if (session) {
            MediaSubsessionIterator iter(*session);
            MediaSubsession* sub;
            while ((sub = iter.next()) != nullptr) {
                sub->deInitiate();
            }
            Medium::close(session);
            session = nullptr;
        }

        // 3. Close client
        if (rtspClient) {
            Medium::close(rtspClient);
            rtspClient = nullptr;
        }

        if (setupIter) { delete setupIter; setupIter = nullptr; }
        videoSub = nullptr;
        audioSub = nullptr;

        // 4. Free authenticator
        if (auth) { delete auth; auth = nullptr; }

        // 5. Reclaim environment (must be last)
        if (env) { env->reclaim(); env = nullptr; }

        // 6. Delete scheduler
        if (scheduler) { delete scheduler; scheduler = nullptr; }
    }
};

// ============================================================================
// VideoFrameSink — receives H.264 NAL units from live555
// ============================================================================
class VideoFrameSink : public MediaSink {
public:
    static VideoFrameSink* createNew(UsageEnvironment& env, CameraRtspWorker::Impl* impl) {
        return new VideoFrameSink(env, impl);
    }
private:
    // Large enough for 4K H.264 I-frames as a single NAL. The old 1 MB buffer
    // truncated big keyframes, and the truncated bytes were silently fed to the
    // decoder (corrupt / green output).
    static constexpr unsigned kBufSize = 4u * 1024 * 1024;  // 4 MB

    VideoFrameSink(UsageEnvironment& env, CameraRtspWorker::Impl* impl)
        : MediaSink(env), impl_(impl) {
        buf_ = new uint8_t[kBufSize];
    }
    ~VideoFrameSink() override { delete[] buf_; }

    Boolean continuePlaying() override {
        if (!fSource) return False;
        fSource->getNextFrame(buf_, kBufSize, afterGettingFrame, this,
                              onSourceClosure, this);
        return True;
    }

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
        auto* self = static_cast<VideoFrameSink*>(clientData);
        if (numTruncatedBytes > 0) {
            // NAL exceeded the buffer: the frame is incomplete. Decoding a
            // truncated NAL yields corrupt output (partial image + green), so
            // drop it instead. (Warning throttled to avoid log spam.)
            if ((self->truncated_count_++ % 30) == 0) {
                self->impl_->owner->L()->warn("RTSP: dropped truncated video NAL ({} bytes kept, "
                             "{} lost; buffer={} B)",
                             frameSize, numTruncatedBytes, kBufSize);
            }
        } else if (self->impl_ && frameSize > 0) {
            // Hand off to the decode worker; return immediately so the network
            // socket keeps being read (no server-side packet drops).
            self->impl_->enqueueNal(self->buf_, frameSize, presentationTime);
        }
        self->continuePlaying();
    }

    CameraRtspWorker::Impl* impl_;
    uint8_t* buf_;
    unsigned truncated_count_ = 0;
};

// ============================================================================
// AudioFrameSink — receives audio (e.g. mu-law) samples from live555
// ============================================================================
class AudioFrameSink : public MediaSink {
public:
    static AudioFrameSink* createNew(UsageEnvironment& env, CameraRtspWorker::Impl* impl) {
        return new AudioFrameSink(env, impl);
    }
private:
    AudioFrameSink(UsageEnvironment& env, CameraRtspWorker::Impl* impl)
        : MediaSink(env), impl_(impl) {
        buf_ = new uint8_t[8192];
    }
    ~AudioFrameSink() override { delete[] buf_; }

    Boolean continuePlaying() override {
        if (!fSource) return False;
        fSource->getNextFrame(buf_, 8192, afterGettingFrame, this,
                              onSourceClosure, this);
        return True;
    }

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned /*numTruncatedBytes*/,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
        auto* self = static_cast<AudioFrameSink*>(clientData);
        auto* impl = self->impl_;

        if (impl && impl->owner && frameSize > 0) {
            CameraAudioPacket pkt;
            pkt.data.assign(self->buf_, self->buf_ + frameSize);
            pkt.sample_rate = impl->audio_sample_rate;
            pkt.channels = impl->audio_channels;
            pkt.codec_id = impl->audio_codec_id;
            pkt.bits_per_sample = impl->audio_bits_per_sample;

            // Check recording state via Impl wrapper (avoids protected access
            // problem from AudioFrameSink, which is a separate class).
            impl->tryComputeRecordingPcTsRel(presentationTime, pkt.pc_ts_rel);

            if (impl->owner->on_audio_packet) {
                impl->owner->on_audio_packet(pkt);
            }
        }
        self->continuePlaying();
    }

    CameraRtspWorker::Impl* impl_;
    uint8_t* buf_;
};

// ============================================================================
// Custom RTSPClient session (back-pointer to Impl)
// ============================================================================
class RtspClientSession : public RTSPClient {
public:
    static RtspClientSession* createNew(UsageEnvironment& env, const char* url,
                                         CameraRtspWorker::Impl* impl) {
        return new RtspClientSession(env, url, impl);
    }
    CameraRtspWorker::Impl* impl_ = nullptr;
protected:
    RtspClientSession(UsageEnvironment& env, const char* url, CameraRtspWorker::Impl* impl)
        : RTSPClient(env, url, 0, "MultisensorLogger", 0, -1), impl_(impl) {}
    ~RtspClientSession() override = default;
};

// ============================================================================
// Audio codec name → AVCodecID mapping (for known RTSP audio formats)
// ============================================================================
static int audioCodecNameToId(const char* name) {
    if (!name) return 0;
    if (iequals(name, "PCMU") == 0)  return AV_CODEC_ID_PCM_MULAW;
    if (iequals(name, "PCMA") == 0)  return AV_CODEC_ID_PCM_ALAW;
    if (iequals(name, "L16")  == 0)  return AV_CODEC_ID_PCM_S16BE;
    if (iequals(name, "MPEG4-GENERIC") == 0) return AV_CODEC_ID_AAC;
    if (iequals(name, "OPUS") == 0)  return AV_CODEC_ID_OPUS;
    return 0;
}

// ============================================================================
// Setup next subsession (walks through video + audio one at a time)
// ============================================================================
static void setupNextSubsession(RTSPClient* client) {
    auto* c = static_cast<RtspClientSession*>(client);
    auto* impl = c->impl_;

    MediaSubsession* sub = nullptr;
    while (impl->setupIter && (sub = impl->setupIter->next()) != nullptr) {
        const char* medium = sub->mediumName();
        const char* codec = sub->codecName();

        if (strcmp(medium, "video") == 0) {
            if (!sub->initiate()) continue;
            impl->videoSub = sub;
            client->sendSetupCommand(*sub, onSetupResponse, False, True, False, impl->auth);
            return;
        }
        if (strcmp(medium, "audio") == 0) {
            if (!sub->initiate()) {
                impl->owner->L()->warn("RTSP: Failed to initiate audio subsession (codec={})",
                             codec ? codec : "?");
                continue;
            }
            impl->audioSub = sub;
            impl->audio_sample_rate = sub->rtpTimestampFrequency();
            impl->audio_channels = sub->numChannels();
            impl->audio_codec_id = audioCodecNameToId(codec);
            // bits_per_sample for mu-law/A-law = 8, L16 = 16
            if (impl->audio_codec_id == AV_CODEC_ID_PCM_MULAW ||
                impl->audio_codec_id == AV_CODEC_ID_PCM_ALAW) {
                impl->audio_bits_per_sample = 8;
            } else if (impl->audio_codec_id == AV_CODEC_ID_PCM_S16BE) {
                impl->audio_bits_per_sample = 16;
            }
            impl->owner->L()->info("RTSP: Audio subsession — codec={}, rate={}Hz, channels={}",
                         codec ? codec : "?",
                         impl->audio_sample_rate, impl->audio_channels);
            client->sendSetupCommand(*sub, onSetupResponse, False, True, False, impl->auth);
            return;
        }
    }

    // No more subsessions to set up — issue PLAY for the entire session
    if (impl->session) {
        client->sendPlayCommand(*impl->session, onPlayResponse, 0.0f, -1.0f, 1.0f, impl->auth);
    } else {
        impl->owner->L()->error("RTSP: No session to play");
        impl->watchVariable = 1;
    }
}

// ============================================================================
// RTSP response handlers
// ============================================================================
static void onDescribeResponse(RTSPClient* client, int resultCode, char* resultString) {
    auto* c = static_cast<RtspClientSession*>(client);
    auto* impl = c->impl_;

    if (resultCode != 0) {
        impl->owner->L()->error("RTSP DESCRIBE failed: {}", resultString ? resultString : "");
        delete[] resultString;
        impl->watchVariable = 1;
        return;
    }

    impl->session = MediaSession::createNew(client->envir(), resultString);
    delete[] resultString;

    if (!impl->session || !impl->session->hasSubsessions()) {
        impl->owner->L()->error("RTSP: No subsessions");
        impl->watchVariable = 1;
        return;
    }

    // Begin walking through subsessions (video + audio)
    impl->setupIter = new MediaSubsessionIterator(*impl->session);
    setupNextSubsession(client);
}

static void onSetupResponse(RTSPClient* client, int resultCode, char* resultString) {
    auto* c = static_cast<RtspClientSession*>(client);
    auto* impl = c->impl_;
    delete[] resultString;

    if (resultCode != 0) {
        impl->owner->L()->warn("RTSP SETUP failed for one subsession (continuing)");
        // Try next subsession even if this one failed
        setupNextSubsession(client);
        return;
    }

    // Determine which subsession just completed by checking which has no sink yet
    if (impl->videoSub && !impl->videoSub->sink) {
        auto* sink = VideoFrameSink::createNew(client->envir(), impl);
        impl->videoSub->sink = sink;
        sink->startPlaying(*impl->videoSub->readSource(), nullptr, nullptr);
    }
    if (impl->audioSub && !impl->audioSub->sink) {
        auto* sink = AudioFrameSink::createNew(client->envir(), impl);
        impl->audioSub->sink = sink;
        sink->startPlaying(*impl->audioSub->readSource(), nullptr, nullptr);
    }

    // Move to next subsession
    setupNextSubsession(client);
}

static void onPlayResponse(RTSPClient* client, int resultCode, char* resultString) {
    auto* c = static_cast<RtspClientSession*>(client);
    auto* impl = c->impl_;
    delete[] resultString;
    if (resultCode != 0)
        impl->owner->L()->error("RTSP PLAY failed");
    else
        impl->owner->L()->info("RTSP: Streaming started");
}

// ============================================================================
// CameraRtspWorker
// ============================================================================
bool CameraRtspWorker::onConnect() {
    impl_ = new Impl();
    impl_->owner = this;

    if (!impl_->openDecoder()) {
        delete impl_; impl_ = nullptr;
        notifyError("RTSP: Failed to init H264 decoder");
        return false;
    }

    impl_->scheduler = BasicTaskScheduler::createNew();
    impl_->env = BasicUsageEnvironment::createNew(*impl_->scheduler);
    impl_->watchVariable = 0;

    impl_->rtspClient = RtspClientSession::createNew(*impl_->env, url_.c_str(), impl_);
    if (!impl_->rtspClient) {
        notifyError("RTSP: Failed to create client");
        return false;
    }

    // Parse authentication from URL (rtsp://user:pass@host/...)
    std::string user, pass;
    if (parseRtspCredentials(url_, user, pass)) {
        impl_->auth = new Authenticator(user.c_str(), pass.c_str());
        L()->info("RTSP: Using Digest/Basic auth for user '{}'", user);
    }

    // Start RTSP handshake (with auth if available)
    impl_->rtspClient->sendDescribeCommand(onDescribeResponse, impl_->auth);

    L()->info("RTSP: Connecting to {}", url_);
    return true;
}

void CameraRtspWorker::pollLoop() {
    if (!impl_ || !impl_->env) return;
    impl_->env->taskScheduler().doEventLoop(&impl_->watchVariable);
}

void CameraRtspWorker::onDisconnect() {
    if (!impl_) return;
    impl_->watchVariable = 1;
    impl_->shutdownStream();
    impl_->closeDecoder();
    delete impl_;
    impl_ = nullptr;
    L()->info("RTSP: Disconnected");
}

void CameraRtspWorker::signalEventLoopExit() {
    if (impl_) {
        impl_->watchVariable = 1;
    }
}

} // namespace msl
