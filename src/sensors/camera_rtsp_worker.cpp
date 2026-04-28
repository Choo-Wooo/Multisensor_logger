#include "camera_rtsp_worker.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>

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

    // Compute pc_ts_rel from a presentationTime. Captures the first call
    // after a reset() as the origin.
    double computePcTsRel(struct timeval pt) {
        int64_t pt_us = tv_to_us(pt);
        int64_t origin = owner->rec_origin_us_.load(std::memory_order_relaxed);
        if (origin < 0) {
            // First packet after recording started — capture origin
            owner->rec_origin_us_.store(pt_us, std::memory_order_relaxed);
            return 0.0;
        }
        return (pt_us - origin) * 1e-6;
    }

    // Public-friendly wrapper — checks recording_ (which Impl can access as
    // a nested type of CameraRtspWorker) and only then computes pc_ts_rel.
    // Used by AudioFrameSink (which is a separate class without privileged access).
    bool tryComputeRecordingPcTsRel(struct timeval pt, double& out_pc_ts_rel) {
        if (!owner->recording_) return false;
        out_pc_ts_rel = computePcTsRel(pt);
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

        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&codecCtx);
            return false;
        }

        frame = av_frame_alloc();
        rgbFrame = av_frame_alloc();
        pkt = av_packet_alloc();
        codecOpened = true;
        return frame && rgbFrame && pkt;
    }

    // Decode H.264 NAL with associated presentationTime.
    // The packet's pts is set to presentationTime (microseconds) so the
    // decoder propagates it to the output AVFrame.
    void decodeNalUnit(const uint8_t* data, unsigned size, struct timeval pt) {
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

            // Use presentationTime-derived pc_ts_rel (no decode/queue jitter)
            if (owner->recording_) {
                struct timeval pt;
                pt.tv_sec  = static_cast<long>(frame->pts / 1000000LL);
                pt.tv_usec = static_cast<long>(frame->pts % 1000000LL);
                cf.pc_ts_rel = computePcTsRel(pt);
            }
            owner->frame_count_++;

            owner->on_frame_ready(cf);
        }
    }

    void closeDecoder() {
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
    VideoFrameSink(UsageEnvironment& env, CameraRtspWorker::Impl* impl)
        : MediaSink(env), impl_(impl) {
        buf_ = new uint8_t[1000000];
    }
    ~VideoFrameSink() override { delete[] buf_; }

    Boolean continuePlaying() override {
        if (!fSource) return False;
        fSource->getNextFrame(buf_, 1000000, afterGettingFrame, this,
                              onSourceClosure, this);
        return True;
    }

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned /*numTruncatedBytes*/,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
        auto* self = static_cast<VideoFrameSink*>(clientData);
        if (self->impl_) {
            self->impl_->decodeNalUnit(self->buf_, frameSize, presentationTime);
        }
        self->continuePlaying();
    }

    CameraRtspWorker::Impl* impl_;
    uint8_t* buf_;
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
