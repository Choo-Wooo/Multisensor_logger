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

/// 재생 프리뷰도 라이브와 동일하게 화면 크기로 줄여서 GPU 로 넘긴다.
static constexpr int kPreviewMaxWidth = 1280;

struct CameraPrefetch::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    uint8_t* rgb_buffer = nullptr;   // 스케일 출력 버퍼 (I420)
    int  out_width = 0;
    int  out_height = 0;
    bool scale_ready = false;
    bool scale_needed = false;
    bool full_range = false;

    /// 디코딩된 프레임을 화면용 I420 (필요 시 축소)으로 변환해 CameraFrame 에 채운다.
    void fillOutput(CameraFrame& out) {
        // 라이브 경로와 동일하게 I420 을 그대로 내보낸다. 색공간 변환은
        // GPU 셰이더가 하고, 4K 세션도 화면 크기로 줄여서 전달한다.
        if (!scale_ready) {
            out_width = width;
            out_height = height;
            if (width > kPreviewMaxWidth) {
                out_width = kPreviewMaxWidth & ~1;
                out_height =
                (int)((int64_t)height * out_width / width) & ~1;
                if (out_height <= 0) out_height = height;
            }
            AVPixelFormat src = static_cast<AVPixelFormat>(frame->format);
            full_range = (src == AV_PIX_FMT_YUVJ420P);
            if (src == AV_PIX_FMT_YUVJ420P) src = AV_PIX_FMT_YUV420P;
            const bool direct = (src == AV_PIX_FMT_YUV420P &&
                         out_width == width &&
                         out_height == height);
            if (!direct) {
                sws_ctx = sws_getContext(
                width, height, src,
                out_width, out_height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
                int sz = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, out_width,
                                      out_height, 1);
                rgb_buffer = static_cast<uint8_t*>(av_malloc(sz));
                av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize,
                             rgb_buffer, AV_PIX_FMT_YUV420P,
                             out_width, out_height, 1);
            }
            scale_needed = !direct;
            scale_ready = true;
        }

        if (scale_needed && sws_ctx) {
            sws_scale(sws_ctx,
                  frame->data, frame->linesize, 0, height,
                  rgb_frame->data, rgb_frame->linesize);
        }

        const int ow = out_width, oh = out_height;
        out.width = ow;
        out.height = oh;
        out.src_width = width;
        out.src_height = height;
        out.yuv_full_range = full_range;
        const size_t ySize = (size_t)ow * oh, uvSize = (size_t)(ow / 2) * (oh / 2);
        out.yuv_data.resize(ySize + uvSize * 2);
        uint8_t* const* sd = scale_needed ? rgb_frame->data : frame->data;
        const int* slz = scale_needed ? rgb_frame->linesize
                                 : frame->linesize;
        uint8_t* dp[3] = {out.yuv_data.data(), out.yuv_data.data() + ySize,
                      out.yuv_data.data() + ySize + uvSize};
        const int dl[3] = {ow, ow / 2, ow / 2};
        for (int p = 0; p < 3; ++p) {
            const int h = (p == 0) ? oh : oh / 2;
            for (int y = 0; y < h; ++y)
                std::memcpy(dp[p] + (size_t)y * dl[p], sd[p] + (size_t)y * slz[p], dl[p]);
        }

    }


    int video_stream_idx = -1;
    int audio_stream_idx = -1;  // -1 if no audio stream in container
    int width = 0;
    int height = 0;

    // Last decoded frame kept for repeat access
    CameraFrame last_frame;
    int last_frame_idx = -1;

    // Audio PTS tracking (for diagnostics — confirms audio stream presence and sync).
    // We don't decode audio data, just observe its packet PTS.
    double audio_first_pts = -1.0;
    double audio_last_pts = -1.0;
    int audio_packets_seen = 0;
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
        auto type = impl_->fmt_ctx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && impl_->video_stream_idx < 0) {
            impl_->video_stream_idx = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO && impl_->audio_stream_idx < 0) {
            impl_->audio_stream_idx = static_cast<int>(i);
        }
    }
    if (impl_->video_stream_idx < 0) { close(); return false; }
    if (impl_->audio_stream_idx >= 0) {
        spdlog::info("CameraPrefetch: Audio stream detected at index {} "
                     "(used as sync master clock)", impl_->audio_stream_idx);
    } else {
        spdlog::info("CameraPrefetch: No audio stream — video PTS only");
    }

    auto* par = impl_->fmt_ctx->streams[impl_->video_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) { close(); return false; }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(impl_->codec_ctx, par);
    impl_->codec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    // 파일 재생은 지연보다 처리량이 중요하다. LOW_DELAY 를 켜면 프레임 병렬
    // 디코딩이 사실상 꺼져서 4K 한 장에 수십 ms 가 걸린다.
    // thread_count = 0 → 코어 수에 맞춰 자동, 프레임+슬라이스 병렬 모두 허용.
    impl_->codec_ctx->thread_count = 0;
    impl_->codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(impl_->codec_ctx, codec, nullptr) < 0) { close(); return false; }

    impl_->width = impl_->codec_ctx->width;
    impl_->height = impl_->codec_ctx->height;
    impl_->frame = av_frame_alloc();
    impl_->rgb_frame = av_frame_alloc();
    impl_->pkt = av_packet_alloc();

    // Build PTS array — also track audio PTS for diagnostics
    AVStream* vs = impl_->fmt_ctx->streams[impl_->video_stream_idx];
    double v_tb = av_q2d(vs->time_base);
    double a_tb = (impl_->audio_stream_idx >= 0)
                  ? av_q2d(impl_->fmt_ctx->streams[impl_->audio_stream_idx]->time_base)
                  : 0.0;

    av_seek_frame(impl_->fmt_ctx, impl_->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);

    AVPacket scan_pkt;
    while (av_read_frame(impl_->fmt_ctx, &scan_pkt) >= 0) {
        if (scan_pkt.stream_index == impl_->video_stream_idx) {
            pts_.push_back(scan_pkt.pts * v_tb);
        } else if (scan_pkt.stream_index == impl_->audio_stream_idx) {
            double a_pts = scan_pkt.pts * a_tb;
            if (impl_->audio_first_pts < 0) impl_->audio_first_pts = a_pts;
            impl_->audio_last_pts = a_pts;
            impl_->audio_packets_seen++;
        }
        av_packet_unref(&scan_pkt);
    }
    total_frames_ = static_cast<int>(pts_.size());

    if (impl_->audio_packets_seen > 0) {
        double audio_duration = impl_->audio_last_pts - impl_->audio_first_pts;
        double video_duration = pts_.empty() ? 0.0 : (pts_.back() - pts_.front());
        spdlog::info("CameraPrefetch: audio={} pkts ({:.2f}s), video={} frames ({:.2f}s), "
                     "drift={:+.3f}s",
                     impl_->audio_packets_seen, audio_duration,
                     total_frames_, video_duration,
                     audio_duration - video_duration);
    }

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
    if (frame_idx == impl_->last_frame_idx &&
        (impl_->last_frame.hasYuv() || impl_->last_frame.hasRgb())) {
        out = impl_->last_frame;
        return true;
    }

    // 앞으로 조금 건너뛰는 경우(재생 중 기준 센서가 카메라보다 느릴 때 흔함)는
    // 시크하지 않고 그대로 디코딩해서 따라간다. 시크는 flush + 키프레임부터
    // 재디코딩이라 훨씬 비싸다. 중간 프레임은 화면에 쓰지 않으므로 색공간
    // 변환/복사 없이 디코딩만 하고 버린다.
    if (frame_idx > current_pos_ && frame_idx - current_pos_ <= kMaxForwardDecode) {
        while (current_pos_ + 1 < frame_idx) {
            if (!decodeNextFrame(out, /*want_output=*/false)) return false;
            current_pos_++;
        }
        if (decodeNextFrame(out, /*want_output=*/true)) {
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

bool CameraPrefetch::decodeNextFrame(CameraFrame& out, bool want_output) {
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
            if (want_output) impl_->fillOutput(out);
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
                impl_->fillOutput(out);

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
