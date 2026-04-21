#include "camera_logger.h"
#include "core/thread_safe_queue.h"
#include "core/clock.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace msl {

namespace {

struct CameraEncodeJob {
    CameraFrame frame;
    double enqueue_steady = 0.0;
};

struct CameraPerfWindow {
    double report_start = 0.0;
    uint64_t frames = 0;
    double total_queue_wait_ms = 0.0;
    double max_queue_wait_ms = 0.0;
    double total_convert_ms = 0.0;
    double max_convert_ms = 0.0;
    double total_encode_ms = 0.0;
    double max_encode_ms = 0.0;
    double total_frame_ms = 0.0;
    double max_frame_ms = 0.0;

    void reset(double now) {
        report_start = now;
        frames = 0;
        total_queue_wait_ms = 0.0;
        max_queue_wait_ms = 0.0;
        total_convert_ms = 0.0;
        max_convert_ms = 0.0;
        total_encode_ms = 0.0;
        max_encode_ms = 0.0;
        total_frame_ms = 0.0;
        max_frame_ms = 0.0;
    }
};

void updateMaxValue(std::atomic<size_t>& target, size_t candidate) {
    size_t current = target.load(std::memory_order_relaxed);
    while (candidate > current &&
           !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {
    }
}

} // namespace

struct CameraLogger::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    int64_t pts_counter = 0;

    // Encoding runs on a separate thread with a queue
    ThreadSafeQueue<CameraEncodeJob, 30> encode_queue;  // Max 30 frames buffered
    std::thread encode_thread;
    std::atomic<bool> encode_running{false};
    std::atomic<uint64_t> dropped_frames{0};
    std::atomic<size_t> max_queue_depth{0};

    int src_width = 0;   // Actual input frame size (may differ from output)
    int src_height = 0;

    void logPerf(const CameraPerfWindow& perf) {
        if (perf.frames == 0) return;

        spdlog::debug(
            "CameraLogger perf: frames={}, queue={}, max_queue={}, avg_wait_ms={:.2f}, max_wait_ms={:.2f}, avg_convert_ms={:.2f}, max_convert_ms={:.2f}, avg_encode_ms={:.2f}, max_encode_ms={:.2f}, avg_frame_ms={:.2f}, max_frame_ms={:.2f}, queue_drops={}",
            perf.frames,
            encode_queue.size(),
            max_queue_depth.load(std::memory_order_relaxed),
            perf.total_queue_wait_ms / static_cast<double>(perf.frames),
            perf.max_queue_wait_ms,
            perf.total_convert_ms / static_cast<double>(perf.frames),
            perf.max_convert_ms,
            perf.total_encode_ms / static_cast<double>(perf.frames),
            perf.max_encode_ms,
            perf.total_frame_ms / static_cast<double>(perf.frames),
            perf.max_frame_ms,
            dropped_frames.load(std::memory_order_relaxed));
    }

    void encodeLoop(int out_width, int out_height) {
        CameraPerfWindow perf;
        perf.reset(Clock::steady());

        while (encode_running || !encode_queue.empty()) {
            CameraEncodeJob job;
            if (!encode_queue.try_pop(job, std::chrono::milliseconds(100))) {
                continue;
            }

            double frame_begin = Clock::steady();
            double queue_wait_ms = (frame_begin - job.enqueue_steady) * 1000.0;
            const CameraFrame& cf = job.frame;

            if (!codec_ctx || !frame) continue;
            if (cf.width <= 0 || cf.height <= 0 || cf.rgb_data.empty()) continue;

            // Recreate sws_ctx if input resolution changed or first frame
            if (!sws_ctx || cf.width != src_width || cf.height != src_height) {
                if (sws_ctx) sws_freeContext(sws_ctx);
                src_width = cf.width;
                src_height = cf.height;
                sws_ctx = sws_getContext(
                    src_width, src_height, AV_PIX_FMT_RGB24,
                    out_width, out_height, AV_PIX_FMT_YUV420P,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
                spdlog::info("CameraLogger: Input {}x{} -> Output {}x{}",
                             src_width, src_height, out_width, out_height);
            }

            if (!sws_ctx) continue;

            // RGB -> YUV (with resize if input != output)
            const uint8_t* src_data[1] = {cf.rgb_data.data()};
            int src_linesize[1] = {cf.width * 3};

            av_frame_make_writable(frame);
            double convert_begin = Clock::steady();
            sws_scale(sws_ctx,
                      src_data, src_linesize, 0, src_height,
                      frame->data, frame->linesize);
            double convert_ms = (Clock::steady() - convert_begin) * 1000.0;

            // VFR: use pc_ts_rel as PTS (milliseconds)
            frame->pts = static_cast<int64_t>(cf.pc_ts_rel * 1000.0);

            // Encode
            double encode_begin = Clock::steady();
            int ret = avcodec_send_frame(codec_ctx, frame);
            if (ret < 0) continue;

            while (ret >= 0) {
                ret = avcodec_receive_packet(codec_ctx, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            double encode_ms = (Clock::steady() - encode_begin) * 1000.0;
            double frame_ms = (Clock::steady() - frame_begin) * 1000.0;

            perf.frames++;
            perf.total_queue_wait_ms += queue_wait_ms;
            perf.total_convert_ms += convert_ms;
            perf.total_encode_ms += encode_ms;
            perf.total_frame_ms += frame_ms;
            if (queue_wait_ms > perf.max_queue_wait_ms) perf.max_queue_wait_ms = queue_wait_ms;
            if (convert_ms > perf.max_convert_ms) perf.max_convert_ms = convert_ms;
            if (encode_ms > perf.max_encode_ms) perf.max_encode_ms = encode_ms;
            if (frame_ms > perf.max_frame_ms) perf.max_frame_ms = frame_ms;

            pts_counter++;

            double now = Clock::steady();
            if (now - perf.report_start >= 2.0) {
                logPerf(perf);
                perf.reset(now);
            }
        }

        logPerf(perf);
    }
};

CameraLogger::CameraLogger(const std::string& mp4_path, int width, int height, int fps)
    : mp4_path_(mp4_path), width_(width), height_(height), fps_(fps) {}

CameraLogger::~CameraLogger() {
    stop();
}

bool CameraLogger::start() {
    impl_ = new Impl();

    // Normalize path for FFmpeg
    std::string path = mp4_path_;
    for (auto& c : path) { if (c == '\\') c = '/'; }

    // Allocate output format context (MP4)
    int ret = avformat_alloc_output_context2(&impl_->fmt_ctx, nullptr, nullptr, path.c_str());
    if (ret < 0 || !impl_->fmt_ctx) {
        spdlog::error("CameraLogger: Failed to create output context");
        delete impl_; impl_ = nullptr;
        return false;
    }

    // Find H.264 encoder (libx264)
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        spdlog::error("CameraLogger: H264 encoder not found");
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    // Create stream
    impl_->stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
    if (!impl_->stream) {
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    // Setup codec context for VFR with millisecond precision
    impl_->codec_ctx = avcodec_alloc_context3(codec);
    impl_->codec_ctx->width = width_;
    impl_->codec_ctx->height = height_;
    impl_->codec_ctx->time_base = {1, 1000};
    impl_->codec_ctx->framerate = {fps_, 1};
    impl_->codec_ctx->gop_size = fps_;
    impl_->codec_ctx->max_b_frames = 0;
    impl_->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->codec_ctx->bit_rate = 4000000;

    // Fast encoding preset to reduce CPU load
    av_opt_set(impl_->codec_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(impl_->codec_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(impl_->codec_ctx->priv_data, "crf", "23", 0);

    if (impl_->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        impl_->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
    if (ret < 0) {
        spdlog::error("CameraLogger: Failed to open encoder");
        avcodec_free_context(&impl_->codec_ctx);
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    avcodec_parameters_from_context(impl_->stream->codecpar, impl_->codec_ctx);
    impl_->stream->time_base = impl_->codec_ctx->time_base;

    if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&impl_->fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            spdlog::error("CameraLogger: Failed to open output file");
            avcodec_free_context(&impl_->codec_ctx);
            avformat_free_context(impl_->fmt_ctx);
            delete impl_; impl_ = nullptr;
            return false;
        }
    }

    ret = avformat_write_header(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        spdlog::error("CameraLogger: Failed to write header");
        avio_closep(&impl_->fmt_ctx->pb);
        avcodec_free_context(&impl_->codec_ctx);
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    impl_->frame = av_frame_alloc();
    impl_->frame->format = AV_PIX_FMT_YUV420P;
    impl_->frame->width = width_;
    impl_->frame->height = height_;
    av_frame_get_buffer(impl_->frame, 0);

    impl_->pkt = av_packet_alloc();

    // sws_ctx is created lazily in encodeLoop on first frame
    // (handles input resolution != output resolution for resizing)

    // Start encoding thread
    impl_->encode_running = true;
    impl_->encode_thread = std::thread(&Impl::encodeLoop, impl_, width_, height_);

    running_ = true;
    spdlog::info("CameraLogger: Started ({}x{}, ultrafast preset)", width_, height_);
    return true;
}

void CameraLogger::writeFrame(const CameraFrame& camera_frame) {
    if (!running_ || !impl_) return;

    // Update resolution from actual frame on first frame
    if (camera_frame.width > 0 && camera_frame.height > 0 &&
        (camera_frame.width != width_ || camera_frame.height != height_)) {
        spdlog::info("CameraLogger: Resolution updated to {}x{}", camera_frame.width, camera_frame.height);
        width_ = camera_frame.width;
        height_ = camera_frame.height;
    }

    CameraEncodeJob job;
    job.frame = camera_frame;
    job.enqueue_steady = Clock::steady();

    bool dropped = impl_->encode_queue.try_push(std::move(job));
    updateMaxValue(impl_->max_queue_depth, impl_->encode_queue.size());

    if (dropped) {
        uint64_t drops = impl_->dropped_frames.fetch_add(1, std::memory_order_relaxed) + 1;
        if (drops == 1 || drops % 100 == 0) {
            spdlog::warn("CameraLogger queue overflow: dropped oldest queued frame (drops={}, depth={})",
                         drops,
                         impl_->encode_queue.size());
        }
    }
}

void CameraLogger::stop() {
    if (!running_ || !impl_) return;
    running_ = false;

    // Signal encoding thread to finish and drain queue
    impl_->encode_running = false;
    if (impl_->encode_thread.joinable()) {
        impl_->encode_thread.join();
    }

    // Flush encoder
    if (impl_->codec_ctx && impl_->pkt && impl_->fmt_ctx) {
        avcodec_send_frame(impl_->codec_ctx, nullptr);
        while (true) {
            int ret = avcodec_receive_packet(impl_->codec_ctx, impl_->pkt);
            if (ret == AVERROR_EOF || ret < 0) break;
            av_packet_rescale_ts(impl_->pkt, impl_->codec_ctx->time_base,
                                 impl_->stream->time_base);
            impl_->pkt->stream_index = impl_->stream->index;
            av_interleaved_write_frame(impl_->fmt_ctx, impl_->pkt);
            av_packet_unref(impl_->pkt);
        }

        av_write_trailer(impl_->fmt_ctx);
    }

    // Cleanup
    if (impl_->sws_ctx)   sws_freeContext(impl_->sws_ctx);
    if (impl_->pkt)       av_packet_free(&impl_->pkt);
    if (impl_->frame)     av_frame_free(&impl_->frame);
    if (impl_->codec_ctx) avcodec_free_context(&impl_->codec_ctx);
    if (impl_->fmt_ctx) {
        if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&impl_->fmt_ctx->pb);
        avformat_free_context(impl_->fmt_ctx);
    }

    spdlog::debug("CameraLogger: Stopped ({} frames encoded, max_queue={}, queue_drops={})",
                  impl_->pts_counter,
                  impl_->max_queue_depth.load(std::memory_order_relaxed),
                  impl_->dropped_frames.load(std::memory_order_relaxed));

    delete impl_;
    impl_ = nullptr;
}

} // namespace msl
