#include "camera_rtsp_worker.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <thread>
#include <mutex>

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
    EventLoopWatchVariable watchVariable{0};
    std::thread eventLoopThread;
    Authenticator* auth = nullptr;  // Digest/Basic authentication

    // FFmpeg decoder
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

    // --- Decoder (following reference pattern) ---

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

        // Low-latency flags for real-time streaming (reference pattern)
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

    void decodeNalUnit(const uint8_t* data, unsigned size) {
        if (!codecOpened || !data || size == 0) return;

        // Prepend Annex-B start code (reference pattern)
        static constexpr uint8_t kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        annexbBuf.resize(4 + size);
        std::memcpy(annexbBuf.data(), kStartCode, 4);
        std::memcpy(annexbBuf.data() + 4, data, size);

        pkt->data = annexbBuf.data();
        pkt->size = static_cast<int>(annexbBuf.size());

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

        // Emit to main thread (skip post if event queue is backing up)
        if (owner && owner->on_frame_ready) {
            CameraFrame cf;
            cf.width = width;
            cf.height = height;
            cf.rgb_data.resize(width * height * 3);
            std::memcpy(cf.rgb_data.data(), rgbBuffer, cf.rgb_data.size());
            if (owner->recording_) cf.pc_ts_rel = owner->getRelativeTime();
            owner->frame_count_++;

            // Call callback directly (caller should write to LatestValue, not EventBus)
            if (owner->on_frame_ready) {
                owner->on_frame_ready(cf);
            }
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

    // --- live555 shutdown (reference pattern: same order) ---

    void shutdownStream() {
        // 1. Stop sink
        if (videoSub && videoSub->sink) {
            videoSub->sink->stopPlaying();
            Medium::close(videoSub->sink);
            videoSub->sink = nullptr;
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

        videoSub = nullptr;

        // 4. Free authenticator
        if (auth) { delete auth; auth = nullptr; }

        // 5. Reclaim environment (must be last)
        if (env) {
            env->reclaim();
            env = nullptr;
        }

        // 5. Delete scheduler
        if (scheduler) {
            delete scheduler;
            scheduler = nullptr;
        }
    }
};

// ============================================================================
// FrameSink - receives H.264 NAL units from live555
// ============================================================================
class FrameSink : public MediaSink {
public:
    static FrameSink* createNew(UsageEnvironment& env, CameraRtspWorker::Impl* impl) {
        return new FrameSink(env, impl);
    }
private:
    FrameSink(UsageEnvironment& env, CameraRtspWorker::Impl* impl)
        : MediaSink(env), impl_(impl) {
        buf_ = new uint8_t[1000000];
    }
    ~FrameSink() override { delete[] buf_; }

    Boolean continuePlaying() override {
        if (!fSource) return False;
        fSource->getNextFrame(buf_, 1000000, afterGettingFrame, this,
                              onSourceClosure, this);
        return True;
    }

    static void afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned, struct timeval, unsigned) {
        auto* self = static_cast<FrameSink*>(clientData);
        if (self->impl_) {
            self->impl_->decodeNalUnit(self->buf_, frameSize);
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

    // Find video subsession
    MediaSubsessionIterator iter(*impl->session);
    MediaSubsession* sub;
    while ((sub = iter.next()) != nullptr) {
        if (strcmp(sub->mediumName(), "video") == 0) {
            if (!sub->initiate()) continue;
            impl->videoSub = sub;
            client->sendSetupCommand(*sub, onSetupResponse, False, True, False, impl->auth);
            return;
        }
    }
    spdlog::error("RTSP: No video subsession");
    impl->watchVariable = 1;
}

static void onSetupResponse(RTSPClient* client, int resultCode, char* resultString) {
    auto* c = static_cast<RtspClientSession*>(client);
    auto* impl = c->impl_;
    delete[] resultString;

    if (resultCode != 0) {
        spdlog::error("RTSP SETUP failed");
        impl->watchVariable = 1;
        return;
    }

    // Create sink and start receiving
    auto* sink = FrameSink::createNew(client->envir(), impl);
    impl->videoSub->sink = sink;
    sink->startPlaying(*impl->videoSub->readSource(), nullptr, nullptr);

    // PLAY
    client->sendPlayCommand(*impl->session, onPlayResponse, 0.0f, -1.0f, 1.0f, impl->auth);
}

static void onPlayResponse(RTSPClient*, int resultCode, char* resultString) {
    delete[] resultString;
    if (resultCode != 0)
        spdlog::error("RTSP PLAY failed");
    else
        spdlog::info("RTSP: Streaming started");
}

// ============================================================================
// CameraRtspWorker (reference pattern: event loop in separate thread)
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

    // Run live555 event loop — blocks until watchVariable != 0
    impl_->env->taskScheduler().doEventLoop(&impl_->watchVariable);
}

void CameraRtspWorker::onDisconnect() {
    if (!impl_) return;

    // Signal event loop to exit
    // (pollLoop is running on this same thread in ISensorWorker::run(),
    //  so by the time onDisconnect() is called, doEventLoop() has already
    //  returned because ISensorWorker sets disconnect_requested_ which
    //  we need to also trigger watchVariable)
    impl_->watchVariable = 1;

    // Clean up live555 resources (safe — event loop has exited)
    impl_->shutdownStream();

    // Clean up FFmpeg
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
