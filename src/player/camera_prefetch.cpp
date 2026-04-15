#include "camera_prefetch.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace msl {

struct CameraPrefetch::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    uint8_t* rgb_buffer = nullptr;
    int video_stream_idx = -1;
    int width = 0;
    int height = 0;

    // Last decoded frame kept for repeat access
    CameraFrame last_frame;
    int last_frame_idx = -1;
};

CameraPrefetch::~CameraPrefetch() { close(); }

bool CameraPrefetch::open(const std::string& mp4_path) {
    mp4_path_ = mp4_path;
    impl_ = new Impl();

    // Normalize path
    std::string path = mp4_path;
    for (auto& c : path) { if (c == '\\') c = '/'; }

    if (avformat_open_input(&impl_->fmt_ctx, path.c_str(), nullptr, nullptr) < 0) {
        char err[128];
        spdlog::error("CameraPrefetch: Failed to open {}", path);
        delete impl_; impl_ = nullptr;
        return false;
    }

    if (avformat_find_stream_info(impl_->fmt_ctx, nullptr) < 0) {
        close(); return false;
    }

    for (unsigned i = 0; i < impl_->fmt_ctx->nb_streams; ++i) {
        if (impl_->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            impl_->video_stream_idx = static_cast<int>(i);
            break;
        }
    }
    if (impl_->video_stream_idx < 0) { close(); return false; }

    auto* par = impl_->fmt_ctx->streams[impl_->video_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) { close(); return false; }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(impl_->codec_ctx, par);
    impl_->codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    impl_->codec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    // Use 2 threads for decoding
    impl_->codec_ctx->thread_count = 2;

    if (avcodec_open2(impl_->codec_ctx, codec, nullptr) < 0) { close(); return false; }

    impl_->width = impl_->codec_ctx->width;
    impl_->height = impl_->codec_ctx->height;
    impl_->frame = av_frame_alloc();
    impl_->rgb_frame = av_frame_alloc();
    impl_->pkt = av_packet_alloc();

    // Build PTS array
    AVStream* vs = impl_->fmt_ctx->streams[impl_->video_stream_idx];
    double tb = av_q2d(vs->time_base);

    av_seek_frame(impl_->fmt_ctx, impl_->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);

    AVPacket scan_pkt;
    while (av_read_frame(impl_->fmt_ctx, &scan_pkt) >= 0) {
        if (scan_pkt.stream_index == impl_->video_stream_idx) {
            pts_.push_back(scan_pkt.pts * tb);
        }
        av_packet_unref(&scan_pkt);
    }
    total_frames_ = static_cast<int>(pts_.size());

    // Reset to beginning
    av_seek_frame(impl_->fmt_ctx, impl_->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(impl_->codec_ctx);
    current_pos_ = -1;

    spdlog::info("CameraPrefetch: {} ({}x{}, {} frames)", path, impl_->width, impl_->height, total_frames_);
    return true;
}

bool CameraPrefetch::getFrame(int frame_idx, CameraFrame& out) {
    std::lock_guard<std::mutex> lock(decode_mutex_);
    if (!impl_ || frame_idx < 0 || frame_idx >= total_frames_) return false;

    // Same frame as last time — return cached
    if (frame_idx == impl_->last_frame_idx && !impl_->last_frame.rgb_data.empty()) {
        out = impl_->last_frame;
        return true;
    }

    // Sequential: next frame (most common during playback)
    if (frame_idx == current_pos_ + 1) {
        if (decodeNextFrame(out)) {
            current_pos_ = frame_idx;
            impl_->last_frame = out;
            impl_->last_frame_idx = frame_idx;
            return true;
        }
        return false;
    }

    // Jump: need to seek
    return seekAndDecode(frame_idx, out);
}

bool CameraPrefetch::decodeNextFrame(CameraFrame& out) {
    if (!impl_) return false;

    while (av_read_frame(impl_->fmt_ctx, impl_->pkt) >= 0) {
        if (impl_->pkt->stream_index != impl_->video_stream_idx) {
            av_packet_unref(impl_->pkt);
            continue;
        }

        int ret = avcodec_send_packet(impl_->codec_ctx, impl_->pkt);
        av_packet_unref(impl_->pkt);
        if (ret < 0) continue;

        if (avcodec_receive_frame(impl_->codec_ctx, impl_->frame) >= 0) {
            // Setup sws once
            if (!impl_->sws_ctx) {
                AVPixelFormat src = static_cast<AVPixelFormat>(impl_->frame->format);
                if (src == AV_PIX_FMT_YUVJ420P) src = AV_PIX_FMT_YUV420P;
                impl_->sws_ctx = sws_getContext(
                    impl_->width, impl_->height, src,
                    impl_->width, impl_->height, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                int sz = av_image_get_buffer_size(AV_PIX_FMT_RGB24, impl_->width, impl_->height, 1);
                impl_->rgb_buffer = static_cast<uint8_t*>(av_malloc(sz));
                av_image_fill_arrays(impl_->rgb_frame->data, impl_->rgb_frame->linesize,
                                     impl_->rgb_buffer, AV_PIX_FMT_RGB24,
                                     impl_->width, impl_->height, 1);
            }

            sws_scale(impl_->sws_ctx,
                      impl_->frame->data, impl_->frame->linesize, 0, impl_->height,
                      impl_->rgb_frame->data, impl_->rgb_frame->linesize);

            out.width = impl_->width;
            out.height = impl_->height;
            out.rgb_data.resize(impl_->width * impl_->height * 3);
            std::memcpy(out.rgb_data.data(), impl_->rgb_buffer, out.rgb_data.size());

            av_frame_unref(impl_->frame);
            return true;
        }
    }
    return false;
}

bool CameraPrefetch::seekAndDecode(int frame_idx, CameraFrame& out) {
    if (!impl_ || frame_idx < 0 || frame_idx >= total_frames_) return false;

    AVStream* vs = impl_->fmt_ctx->streams[impl_->video_stream_idx];
    int64_t target_ts = static_cast<int64_t>(pts_[frame_idx] / av_q2d(vs->time_base));

    av_seek_frame(impl_->fmt_ctx, impl_->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(impl_->codec_ctx);

    // Decode forward to target
    double target_pts = pts_[frame_idx];
    int attempts = 0;

    while (av_read_frame(impl_->fmt_ctx, impl_->pkt) >= 0 && attempts < 200) {
        if (impl_->pkt->stream_index != impl_->video_stream_idx) {
            av_packet_unref(impl_->pkt);
            continue;
        }

        int ret = avcodec_send_packet(impl_->codec_ctx, impl_->pkt);
        av_packet_unref(impl_->pkt);
        if (ret < 0) continue;

        while (avcodec_receive_frame(impl_->codec_ctx, impl_->frame) >= 0) {
            double frame_pts = impl_->frame->pts * av_q2d(vs->time_base);

            if (frame_pts >= target_pts - 0.001) {
                // Setup sws
                if (!impl_->sws_ctx) {
                    AVPixelFormat src = static_cast<AVPixelFormat>(impl_->frame->format);
                    if (src == AV_PIX_FMT_YUVJ420P) src = AV_PIX_FMT_YUV420P;
                    impl_->sws_ctx = sws_getContext(
                        impl_->width, impl_->height, src,
                        impl_->width, impl_->height, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    int sz = av_image_get_buffer_size(AV_PIX_FMT_RGB24, impl_->width, impl_->height, 1);
                    impl_->rgb_buffer = static_cast<uint8_t*>(av_malloc(sz));
                    av_image_fill_arrays(impl_->rgb_frame->data, impl_->rgb_frame->linesize,
                                         impl_->rgb_buffer, AV_PIX_FMT_RGB24,
                                         impl_->width, impl_->height, 1);
                }

                sws_scale(impl_->sws_ctx,
                          impl_->frame->data, impl_->frame->linesize, 0, impl_->height,
                          impl_->rgb_frame->data, impl_->rgb_frame->linesize);

                out.width = impl_->width;
                out.height = impl_->height;
                out.rgb_data.resize(impl_->width * impl_->height * 3);
                std::memcpy(out.rgb_data.data(), impl_->rgb_buffer, out.rgb_data.size());

                current_pos_ = frame_idx;
                impl_->last_frame = out;
                impl_->last_frame_idx = frame_idx;

                av_frame_unref(impl_->frame);
                return true;
            }
            av_frame_unref(impl_->frame);
            attempts++;
        }
    }
    return false;
}

void CameraPrefetch::close() {
    if (impl_) {
        if (impl_->sws_ctx)   sws_freeContext(impl_->sws_ctx);
        if (impl_->rgb_buffer) av_free(impl_->rgb_buffer);
        if (impl_->pkt)       av_packet_free(&impl_->pkt);
        if (impl_->rgb_frame) av_frame_free(&impl_->rgb_frame);
        if (impl_->frame)     av_frame_free(&impl_->frame);
        if (impl_->codec_ctx) avcodec_free_context(&impl_->codec_ctx);
        if (impl_->fmt_ctx)   avformat_close_input(&impl_->fmt_ctx);
        delete impl_;
        impl_ = nullptr;
    }
    pts_.clear();
    total_frames_ = 0;
    current_pos_ = -1;
}

} // namespace msl
