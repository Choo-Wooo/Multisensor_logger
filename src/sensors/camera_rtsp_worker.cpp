#include "camera_rtsp_worker.h"
#include "core/clock.h"
#include <spdlog/spdlog.h>
#include <cstring>
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
    std::vector<uint8_t> annexbBuf;  // reusable buffer for start code prepend

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
        std::vector<uint8_t> data;
        struct timeval pt;
        double arrival_wall = 0.0;  // Clock::now() at network arrival
    };
    std::queue<PendingNal> nalQueue;
    std::mutex nalMutex;
    std::condition_variable nalCv;
    std::thread decodeThread;
    std::atomic<bool> decodeRunning{false};
    unsigned dropped_nals_ = 0;
    static constexpr size_t kMaxQueuedNals = 60;  // safety valve (~few seconds)

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

    struct TsGuard {
        const char* name;
        uint32_t epoch = 0xFFFFFFFFu;
        double last_out = -1.0;
        uint64_t clamped = 0;
    };
    TsGuard videoGuard{"video"};   // used only from the decode thread
    TsGuard audioGuard{"audio"};   // used only from the live555 network thread

    // Maps packet pts (presentationTime µs) → local arrival wall time across
    // the decoder, which may buffer frames. Decode thread only.
    // NOTE: must NOT ride in pkt->opaque — the pointer is 4 bytes on the
    // 32-bit build and silently truncates the 64-bit timestamp.
    std::deque<std::pair<int64_t, double>> pts_arrivals_;

    // Compute pc_ts_rel from a presentationTime (µs) + local arrival wall time.
    // Captures the first call after a resetRecordingClock() as the origin.
    double guardedPcTsRel(int64_t pt_us, double arrival_wall, TsGuard& g) {
        uint32_t epoch = owner->rec_epoch_.load(std::memory_order_relaxed);
        if (g.epoch != epoch) {
            // New recording session — reset per-stream guard state.
            g.epoch = epoch;
            g.last_out = -1.0;
            g.clamped = 0;
        }

        int64_t origin = owner->rec_origin_us_.load(std::memory_order_relaxed);
        if (origin < 0) {
            // First packet after recording started — capture origin
            owner->rec_origin_us_.store(pt_us, std::memory_order_relaxed);
            origin = pt_us;
        }
        double pt_rel = (pt_us - origin) * 1e-6;
        double local_rel = arrival_wall - owner->rec_start_time_.load();

        double lo = local_rel - kMaxTsDriftSec;
        double hi = local_rel + kMaxTsDriftSec;
        double out = pt_rel;
        if (out < lo || out > hi) {
            out = (out < lo) ? lo : hi;
            if ((g.clamped++ % 150) == 0) {
                spdlog::warn("RTSP {}: camera timestamps drifting from real time "
                             "(camera={:.3f}s vs pc={:.3f}s, rate~{:.2f}x) — "
                             "clamping recorded PTS to PC clock",
                             g.name, pt_rel, local_rel,
                             local_rel > 1.0 ? pt_rel / local_rel : 0.0);
            }
        }

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
            spdlog::error("[FFmpeg] {}", buf);
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
    void decodeNalUnit(const uint8_t* data, unsigned size, struct timeval pt,
                       double arrival_wall) {
        if (!codecOpened || !data || size == 0) return;

        // Prepend Annex-B start code
        static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        annexbBuf.resize(4 + size);
        std::memcpy(annexbBuf.data(), kStartCode, 4);
        std::memcpy(annexbBuf.data() + 4, data, size);

        pkt->data = annexbBuf.data();
        pkt->size = static_cast<int>(annexbBuf.size());
        pkt->pts  = tv_to_us(pt);  // microseconds — survives through decoder
        pkt->dts  = pkt->pts;

        // Remember this pts's arrival time for deliverFrame(). Consecutive
        // NALs of one frame (SPS/PPS/slices) share a presentationTime — keep
        // the first (earliest) arrival for that pts.
        if (pts_arrivals_.empty() || pts_arrivals_.back().first != pkt->pts) {
            pts_arrivals_.emplace_back(pkt->pts, arrival_wall);
            if (pts_arrivals_.size() > 64) pts_arrivals_.pop_front();
        }

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

        // Setup sws on first decoded frame
        if (!swsCtx) {
            width = frame->width;
            height = frame->height;

            AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
            bool jpegRange = (srcFmt == AV_PIX_FMT_YUVJ420P ||
                              srcFmt == AV_PIX_FMT_YUVJ422P ||
                              srcFmt == AV_PIX_FMT_YUVJ444P);

            if (srcFmt == AV_PIX_FMT_YUVJ420P) srcFmt = AV_PIX_FMT_YUV420P;
            if (srcFmt == AV_PIX_FMT_YUVJ422P) srcFmt = AV_PIX_FMT_YUV422P;
            if (srcFmt == AV_PIX_FMT_YUVJ444P) srcFmt = AV_PIX_FMT_YUV444P;

            swsCtx = sws_getContext(width, height, srcFmt,
                                    width, height, AV_PIX_FMT_RGB24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (jpegRange && swsCtx) {
                const int* coefs = sws_getCoefficients(SWS_CS_DEFAULT);
                sws_setColorspaceDetails(swsCtx, coefs, 1, coefs, 1, 0, 1 << 16, 1 << 16);
            }

            int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
            rgbBuffer = static_cast<uint8_t*>(av_malloc(bufSize));
            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                                 rgbBuffer, AV_PIX_FMT_RGB24, width, height, 1);

            spdlog::info("RTSP: Decoded frame {}x{}", width, height);
        }

        sws_scale(swsCtx, frame->data, frame->linesize, 0, height,
                  rgbFrame->data, rgbFrame->linesize);

        if (owner && owner->on_frame_ready) {
            CameraFrame cf;
            cf.width = width;
            cf.height = height;
            cf.rgb_data.resize(width * height * 3);
            std::memcpy(cf.rgb_data.data(), rgbBuffer, cf.rgb_data.size());

            // Use presentationTime-derived pc_ts_rel (no decode/queue jitter),
            // sanity-clamped against the frame's local arrival time.
            if (owner->recording_) {
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
    void enqueueNal(const uint8_t* data, unsigned size, struct timeval pt) {
        if (!data || size == 0) return;
        double arrival = Clock::now();
        const uint8_t nalType = data[0] & 0x1F;
        const bool isIdr = (nalType == 5);
        const bool isParamSet = (nalType == 7 || nalType == 8);  // SPS/PPS
        {
            std::lock_guard<std::mutex> lk(nalMutex);

            if (waitingForIdr) {
                if (isIdr) {
                    waitingForIdr = false;
                } else if (!isParamSet) {
                    // Give up after 5s for streams without periodic IDRs.
                    if (arrival - waitingForIdrSince > 5.0) {
                        waitingForIdr = false;
                        spdlog::warn("RTSP: no keyframe within 5s after queue "
                                     "flush — resuming decode anyway");
                    } else {
                        return;  // not decodable until the next keyframe
                    }
                }
            }

            if (nalQueue.size() >= kMaxQueuedNals) {
                // Decoder genuinely can't keep up. Dropping arbitrary NALs
                // corrupts every frame until the next keyframe (missing
                // references), so flush the whole backlog and skip forward to
                // the next IDR instead.
                size_t flushed = nalQueue.size();
                std::queue<PendingNal> empty;
                std::swap(nalQueue, empty);
                if (!isIdr) {
                    waitingForIdr = true;
                    waitingForIdrSince = arrival;
                }
                if ((dropped_nals_++ % 10) == 0) {
                    spdlog::warn("RTSP: decode queue full — flushed {} NALs, "
                                 "skipping to next keyframe (decoder slower "
                                 "than stream)", flushed);
                }
                if (!isIdr && !isParamSet) return;
            }
            nalQueue.emplace();
            PendingNal& slot = nalQueue.back();
            slot.data.assign(data, data + size);
            slot.pt = pt;
            slot.arrival_wall = arrival;
        }
        nalCv.notify_one();
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
            decodeNalUnit(item.data.data(),
                          static_cast<unsigned>(item.data.size()), item.pt,
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
                spdlog::warn("RTSP: dropped truncated video NAL ({} bytes kept, "
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
                spdlog::warn("RTSP: Failed to initiate audio subsession (codec={})",
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
            spdlog::info("RTSP: Audio subsession — codec={}, rate={}Hz, channels={}",
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
        spdlog::error("RTSP: No session to play");
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
        spdlog::error("RTSP DESCRIBE failed: {}", resultString ? resultString : "");
        delete[] resultString;
        impl->watchVariable = 1;
        return;
    }

    impl->session = MediaSession::createNew(client->envir(), resultString);
    delete[] resultString;

    if (!impl->session || !impl->session->hasSubsessions()) {
        spdlog::error("RTSP: No subsessions");
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
        spdlog::warn("RTSP SETUP failed for one subsession (continuing)");
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

static void onPlayResponse(RTSPClient*, int resultCode, char* resultString) {
    delete[] resultString;
    if (resultCode != 0)
        spdlog::error("RTSP PLAY failed");
    else
        spdlog::info("RTSP: Streaming started");
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
        spdlog::info("RTSP: Using Digest/Basic auth for user '{}'", user);
    }

    // Start RTSP handshake (with auth if available)
    impl_->rtspClient->sendDescribeCommand(onDescribeResponse, impl_->auth);

    spdlog::info("RTSP: Connecting to {}", url_);
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
    spdlog::info("RTSP: Disconnected");
}

void CameraRtspWorker::signalEventLoopExit() {
    if (impl_) {
        impl_->watchVariable = 1;
    }
}

} // namespace msl
