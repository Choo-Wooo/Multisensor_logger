#include "camera_logger.h"
#include "core/thread_safe_queue.h"
#include "core/clock.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
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

    // Video
    AVCodecContext* codec_ctx = nullptr;
    AVStream* video_stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    int64_t pts_counter = 0;
    int64_t last_video_pts_ms = -1;   // encode thread only
    int64_t last_audio_pts = -1;      // audio callback thread only

    // Audio (passthrough, no codec context needed)
    AVStream* audio_stream = nullptr;
    int audio_codec_id = 0;
    int audio_sample_rate = 8000;
    int audio_channels = 1;

    // Mutex protects all fmt_ctx writes (video + audio interleave).
    std::mutex muxer_mutex;

    // Dropped audio packet stats
    std::atomic<uint64_t> audio_packets_written{0};
    std::atomic<uint64_t> audio_packets_dropped{0};

    // Encoding runs on a separate thread with a queue
    ThreadSafeQueue<CameraEncodeJob, 30> encode_queue;
    std::thread encode_thread;
    std::atomic<bool> encode_running{false};
    std::atomic<uint64_t> dropped_frames{0};
    std::atomic<size_t> max_queue_depth{0};

    // --- 패스스루(무재인코딩) 상태 ---
    ThreadSafeQueue<CameraNalPacket, 120> nal_queue;
    std::thread mux_thread;
    std::atomic<bool> mux_running{false};
    std::atomic<uint64_t> nal_packets_written{0};
    std::atomic<uint64_t> nal_packets_dropped{0};
    bool header_written = false;       // mux 스레드 + start() 에서만 접근
    std::vector<uint8_t> extradata;    // AnnexB SPS+PPS
    int passthrough_width = 0;
    int passthrough_height = 0;

    /// AnnexB 액세스 유닛에서 SPS(7)/PPS(8) 를 뽑아 extradata 를 만든다.
    /// MOV 머서가 AnnexB extradata 를 avcC 로 변환해 준다.
    bool collectExtradata(const std::vector<uint8_t>& au) {
        size_t i = 0;
        bool found_sps = false, found_pps = false;
        std::vector<uint8_t> out;
        while (i + 4 <= au.size()) {
            if (!(au[i] == 0 && au[i+1] == 0 && au[i+2] == 0 && au[i+3] == 1)) { ++i; continue; }
            size_t nal_start = i + 4;
            size_t j = nal_start;
            while (j + 4 <= au.size() &&
                   !(au[j] == 0 && au[j+1] == 0 && au[j+2] == 0 && au[j+3] == 1)) ++j;
            size_t nal_end = (j + 4 <= au.size()) ? j : au.size();
            if (nal_start < nal_end) {
                uint8_t type = au[nal_start] & 0x1F;
                if (type == 7 || type == 8) {
                    out.insert(out.end(), au.begin() + i, au.begin() + nal_end);
                    if (type == 7) found_sps = true; else found_pps = true;
                }
            }
            i = nal_end;
        }
        if (found_sps && found_pps) { extradata.swap(out); return true; }
        return false;
    }

    void muxLoop();

    int src_width = 0;
    int src_height = 0;

    // --- 병렬 색공간 변환용 밴드 컨텍스트 ---
    std::vector<SwsContext*> sws_bands;
    std::vector<int> band_start;
    std::vector<int> band_height;

    void freeBandContexts() {
        for (auto* c : sws_bands) if (c) sws_freeContext(c);
        sws_bands.clear();
        band_start.clear();
        band_height.clear();
    }

    // 리사이즈 없는 순수 색공간 변환(RGB24 -> YUV420P)일 때만 가로 밴드로 나눠
    // 멀티스레드 변환한다. ARM 에는 이 경로의 NEON 구현이 없어 4K 기준 ~180ms 가
    // 걸리는데, 밴드 분할로 ~25ms 까지 떨어진다. (수직 스케일링이 있으면 밴드
    // 경계에 이음매가 생길 수 있어 적용하지 않는다.)
    void buildBandContexts(int w, int h) {
        freeBandContexts();
        unsigned hw = std::thread::hardware_concurrency();
        int nbands = static_cast<int>(hw ? hw : 4);
        if (nbands > 8) nbands = 8;
        while (nbands > 1 && (h / nbands) < 64) --nbands;
        if (nbands <= 1) return;

        int rows = ((h / nbands) / 2) * 2;   // YUV420 이라 짝수 행 정렬 필요
        if (rows <= 0) return;

        int y = 0;
        for (int i = 0; i < nbands; ++i) {
            int bh = (i == nbands - 1) ? (h - y) : rows;
            if (bh <= 0) break;
            SwsContext* c = sws_getContext(w, bh, AV_PIX_FMT_RGB24,
                                           w, bh, AV_PIX_FMT_YUV420P,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!c) { freeBandContexts(); return; }
            sws_bands.push_back(c);
            band_start.push_back(y);
            band_height.push_back(bh);
            y += bh;
        }
        spdlog::info("CameraLogger: parallel RGB->YUV conversion enabled ({} bands)",
                     sws_bands.size());
    }

    void logPerf(const CameraPerfWindow& perf) {
        if (perf.frames == 0) return;

        spdlog::debug(
            "CameraLogger perf: frames={}, queue={}, max_queue={}, "
            "avg_wait_ms={:.2f}, max_wait_ms={:.2f}, "
            "avg_convert_ms={:.2f}, max_convert_ms={:.2f}, "
            "avg_encode_ms={:.2f}, max_encode_ms={:.2f}, "
            "avg_frame_ms={:.2f}, max_frame_ms={:.2f}, "
            "queue_drops={}, audio_pkts={}, audio_dropped={}",
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
            dropped_frames.load(std::memory_order_relaxed),
            audio_packets_written.load(std::memory_order_relaxed),
            audio_packets_dropped.load(std::memory_order_relaxed));
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
            if (cf.width <= 0 || cf.height <= 0) continue;
            if (!cf.hasYuv() && !cf.hasRgb()) continue;

            // I420 을 그대로 받은 경우: 색공간 변환 없이 인코더에 바로 복사한다.
            // (워커가 디코더 출력 포맷 그대로 넘겨주는 경로)
            if (cf.hasYuv() && cf.width == out_width && cf.height == out_height) {
                av_frame_make_writable(frame);
                double copy_begin = Clock::steady();
                const uint8_t* sp[3] = {cf.planeY(), cf.planeU(), cf.planeV()};
                const int sl[3] = {cf.width, cf.width / 2, cf.width / 2};
                for (int p = 0; p < 3; ++p) {
                    const int h = (p == 0) ? cf.height : cf.height / 2;
                    for (int y = 0; y < h; ++y)
                        std::memcpy(frame->data[p] + (size_t)y * frame->linesize[p],
                                    sp[p] + (size_t)y * sl[p], sl[p]);
                }
                double convert_ms = (Clock::steady() - copy_begin) * 1000.0;

                int64_t pts_ms = static_cast<int64_t>(cf.pc_ts_rel * 1000.0);
                if (pts_ms <= last_video_pts_ms) pts_ms = last_video_pts_ms + 1;
                last_video_pts_ms = pts_ms;
                frame->pts = pts_ms;

                double encode_begin = Clock::steady();
                int ret2 = avcodec_send_frame(codec_ctx, frame);
                if (ret2 >= 0) {
                    while (ret2 >= 0) {
                        ret2 = avcodec_receive_packet(codec_ctx, pkt);
                        if (ret2 == AVERROR(EAGAIN) || ret2 == AVERROR_EOF) break;
                        if (ret2 < 0) break;
                        av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                        pkt->stream_index = video_stream->index;
                        {
                            std::lock_guard<std::mutex> lock(muxer_mutex);
                            av_interleaved_write_frame(fmt_ctx, pkt);
                        }
                        av_packet_unref(pkt);
                    }
                }
                double encode_ms = (Clock::steady() - encode_begin) * 1000.0;
                double frame_ms = (Clock::steady() - frame_begin) * 1000.0;
                perf.frames++;
                perf.total_queue_wait_ms += queue_wait_ms;
                if (queue_wait_ms > perf.max_queue_wait_ms) perf.max_queue_wait_ms = queue_wait_ms;
                perf.total_convert_ms += convert_ms;
                if (convert_ms > perf.max_convert_ms) perf.max_convert_ms = convert_ms;
                perf.total_encode_ms += encode_ms;
                if (encode_ms > perf.max_encode_ms) perf.max_encode_ms = encode_ms;
                perf.total_frame_ms += frame_ms;
                if (frame_ms > perf.max_frame_ms) perf.max_frame_ms = frame_ms;
                pts_counter++;
                double now2 = Clock::steady();
                if (now2 - perf.report_start >= 2.0) { logPerf(perf); perf.reset(now2); }
                continue;
            }
            if (cf.rgb_data.empty()) continue;   // 아래 RGB 경로는 RGB 필수

            // Recreate sws_ctx if input resolution changed or first frame
            if (!sws_ctx || cf.width != src_width || cf.height != src_height) {
                if (sws_ctx) sws_freeContext(sws_ctx);
                freeBandContexts();
                src_width = cf.width;
                src_height = cf.height;
                sws_ctx = sws_getContext(
                    src_width, src_height, AV_PIX_FMT_RGB24,
                    out_width, out_height, AV_PIX_FMT_YUV420P,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                spdlog::info("CameraLogger: Input {}x{} -> Output {}x{}",
                             src_width, src_height, out_width, out_height);
                if (src_width == out_width && src_height == out_height) {
                    buildBandContexts(out_width, out_height);
                }
            }

            if (!sws_ctx) continue;

            const uint8_t* src_data[1] = {cf.rgb_data.data()};
            int src_linesize[1] = {cf.width * 3};

            av_frame_make_writable(frame);
            double convert_begin = Clock::steady();
            if (!sws_bands.empty()) {
                std::vector<std::thread> workers;
                workers.reserve(sws_bands.size());
                for (size_t bi = 0; bi < sws_bands.size(); ++bi) {
                    workers.emplace_back([&, bi]() {
                        const int y0 = band_start[bi];
                        const int bh = band_height[bi];
                        const uint8_t* sd[1] = {
                            cf.rgb_data.data() + static_cast<size_t>(y0) * src_linesize[0] };
                        uint8_t* dd[3] = {
                            frame->data[0] + static_cast<size_t>(y0) * frame->linesize[0],
                            frame->data[1] + static_cast<size_t>(y0 / 2) * frame->linesize[1],
                            frame->data[2] + static_cast<size_t>(y0 / 2) * frame->linesize[2] };
                        sws_scale(sws_bands[bi], sd, src_linesize, 0, bh, dd, frame->linesize);
                    });
                }
                for (auto& w : workers) w.join();
            } else {
                sws_scale(sws_ctx,
                          src_data, src_linesize, 0, src_height,
                          frame->data, frame->linesize);
            }
            double convert_ms = (Clock::steady() - convert_begin) * 1000.0;

            // VFR PTS: pc_ts_rel (seconds) → milliseconds (codec time_base = {1,1000})
            // pc_ts_rel is sourced from live555 presentationTime — jitter-free.
            // Encoder/muxer require strictly increasing PTS: two frames landing
            // in the same millisecond (or a source glitch) must not collide.
            int64_t pts_ms = static_cast<int64_t>(cf.pc_ts_rel * 1000.0);
            if (pts_ms <= last_video_pts_ms) pts_ms = last_video_pts_ms + 1;
            last_video_pts_ms = pts_ms;
            frame->pts = pts_ms;

            double encode_begin = Clock::steady();
            int ret = avcodec_send_frame(codec_ctx, frame);
            if (ret < 0) continue;

            while (ret >= 0) {
                ret = avcodec_receive_packet(codec_ctx, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                {
                    std::lock_guard<std::mutex> lock(muxer_mutex);
                    av_interleaved_write_frame(fmt_ctx, pkt);
                }
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

void CameraLogger::Impl::muxLoop() {
    CameraNalPacket item;
    while (mux_running.load(std::memory_order_relaxed) || !nal_queue.empty()) {
        if (!nal_queue.try_pop(item, std::chrono::milliseconds(200))) continue;
        if (item.data.empty()) continue;

        // 첫 키프레임을 만나면 SPS/PPS 로 extradata 를 세우고 헤더를 기록한다.
        if (!header_written) {
            if (!item.keyframe || !collectExtradata(item.data)) continue;

            AVCodecParameters* p = video_stream->codecpar;
            p->extradata = (uint8_t*)av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!p->extradata) continue;
            std::memcpy(p->extradata, extradata.data(), extradata.size());
            p->extradata_size = static_cast<int>(extradata.size());
            if (passthrough_width > 0 && passthrough_height > 0) {
                p->width = passthrough_width;
                p->height = passthrough_height;
            }

            std::lock_guard<std::mutex> lock(muxer_mutex);
            int ret = avformat_write_header(fmt_ctx, nullptr);
            if (ret < 0) {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                spdlog::error("CameraLogger: passthrough write_header failed: {}", errbuf);
                return;
            }
            header_written = true;
            spdlog::info("CameraLogger: passthrough recording started "
                         "({}x{}, extradata {} B)",
                         p->width, p->height, p->extradata_size);
        }

        int64_t pts_ms = static_cast<int64_t>(item.pc_ts_rel * 1000.0);
        if (pts_ms <= last_video_pts_ms) pts_ms = last_video_pts_ms + 1;
        last_video_pts_ms = pts_ms;

        AVPacket* p = av_packet_alloc();
        if (!p) continue;
        if (av_new_packet(p, static_cast<int>(item.data.size())) < 0) {
            av_packet_free(&p);
            continue;
        }
        std::memcpy(p->data, item.data.data(), item.data.size());
        p->pts = pts_ms;
        p->dts = pts_ms;
        p->duration = 0;
        p->stream_index = video_stream->index;
        if (item.keyframe) p->flags |= AV_PKT_FLAG_KEY;

        {
            std::lock_guard<std::mutex> lock(muxer_mutex);
            av_packet_rescale_ts(p, AVRational{1, 1000}, video_stream->time_base);
            if (av_interleaved_write_frame(fmt_ctx, p) < 0)
                nal_packets_dropped++;
            else
                nal_packets_written++;
        }
        av_packet_free(&p);
    }
}

CameraLogger::CameraLogger(const std::string& mov_path, int width, int height, int fps,
                           Mode mode)
    : mov_path_(mov_path), width_(width), height_(height), fps_(fps), mode_(mode) {}

CameraLogger::~CameraLogger() {
    stop();
}

bool CameraLogger::start() {
    impl_ = new Impl();

    // Normalize path for FFmpeg
    std::string path = mov_path_;
    for (auto& c : path) { if (c == '\\') c = '/'; }

    // MOV container — supports H.264 video + pcm_mulaw audio passthrough
    int ret = avformat_alloc_output_context2(&impl_->fmt_ctx, nullptr, "mov", path.c_str());
    if (ret < 0 || !impl_->fmt_ctx) {
        spdlog::error("CameraLogger: Failed to create output context");
        delete impl_; impl_ = nullptr;
        return false;
    }

    // MOV 머서는 기본값(avoid_negative_ts=auto -> make_zero)으로 "첫 패킷 PTS 가
    // 0 이 되도록 전체 타임스탬프를 평행이동" 한다. 그러면 녹화 시작 이후 이 채널의
    // 첫 프레임이 언제 들어왔는지(=다른 채널/라이다/레이더와의 시작 오프셋)가
    // 파일에서 사라져 재생 시 채널끼리 어긋난다. 우리 PTS 는 항상 0 이상이므로
    // 이동이 필요 없다 — 비활성화해서 pc_ts_rel 을 그대로 보존한다.
    impl_->fmt_ctx->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;

    // -------- Video stream (패스스루) --------
    if (mode_ == Mode::Passthrough) {
        impl_->video_stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
        if (!impl_->video_stream) {
            avformat_free_context(impl_->fmt_ctx);
            delete impl_; impl_ = nullptr;
            return false;
        }
        AVCodecParameters* vp = impl_->video_stream->codecpar;
        vp->codec_type = AVMEDIA_TYPE_VIDEO;
        vp->codec_id   = AV_CODEC_ID_H264;
        vp->width      = width_;
        vp->height     = height_;
        vp->format     = AV_PIX_FMT_YUV420P;
        impl_->video_stream->time_base = {1, 1000};   // 밀리초
        impl_->passthrough_width = width_;
        impl_->passthrough_height = height_;
    } else {

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        spdlog::error("CameraLogger: H264 encoder not found");
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    impl_->video_stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
    if (!impl_->video_stream) {
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }

    impl_->codec_ctx = avcodec_alloc_context3(codec);
    impl_->codec_ctx->width = width_;
    impl_->codec_ctx->height = height_;
    impl_->codec_ctx->time_base = {1, 1000};         // millisecond precision
    impl_->codec_ctx->framerate = {fps_, 1};
    impl_->codec_ctx->gop_size = fps_;
    impl_->codec_ctx->max_b_frames = 0;
    impl_->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    impl_->codec_ctx->bit_rate = 4000000;

    av_opt_set(impl_->codec_ctx->priv_data, "preset", "ultrafast", 0);
    // NOTE: tune=zerolatency 는 x264 의 프레임 병렬 스레딩을 끈다. 파일 녹화에는
    // 지연이 중요하지 않으므로 사용하지 않는다 (4K 인코딩 처리량 크게 개선:
    // Jetson Orin 3840x2160 기준 프레임당 ~72ms -> ~6ms).
    av_opt_set(impl_->codec_ctx->priv_data, "crf", "23", 0);
    impl_->codec_ctx->thread_count = 0;  // 0 = 코어 수에 맞춰 자동

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

    avcodec_parameters_from_context(impl_->video_stream->codecpar, impl_->codec_ctx);
    impl_->video_stream->time_base = impl_->codec_ctx->time_base;
    }   // end Mode::Encode

    // -------- Audio stream (passthrough) --------
    // Created up-front; first audio packet writes will determine the codec.
    // Default: pcm_mulaw 8kHz mono (most common for IP cams).
    impl_->audio_stream = avformat_new_stream(impl_->fmt_ctx, nullptr);
    if (impl_->audio_stream) {
        AVCodecParameters* p = impl_->audio_stream->codecpar;
        p->codec_type = AVMEDIA_TYPE_AUDIO;
        p->codec_id = AV_CODEC_ID_PCM_MULAW;
        p->sample_rate = 8000;
        p->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
        p->ch_layout.nb_channels = 1;
        p->ch_layout.u.mask = AV_CH_LAYOUT_MONO;
        p->bits_per_coded_sample = 8;
        p->bit_rate = 64000;
        p->frame_size = 160;  // 20ms at 8000 Hz
        impl_->audio_stream->time_base = {1, 8000};
        impl_->audio_codec_id = AV_CODEC_ID_PCM_MULAW;
        impl_->audio_sample_rate = 8000;
        impl_->audio_channels = 1;
    }

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

    if (mode_ == Mode::Passthrough) {
        // 헤더는 첫 키프레임에서 SPS/PPS 를 확보한 뒤 mux 스레드가 기록한다.
        impl_->mux_running = true;
        impl_->mux_thread = std::thread(&Impl::muxLoop, impl_);
        running_ = true;
        spdlog::info("CameraLogger: Started ({}x{}, MOV passthrough — 재인코딩 없음)",
                     width_, height_);
        return true;
    }

    ret = avformat_write_header(impl_->fmt_ctx, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        spdlog::error("CameraLogger: Failed to write header: {}", errbuf);
        avio_closep(&impl_->fmt_ctx->pb);
        avcodec_free_context(&impl_->codec_ctx);
        avformat_free_context(impl_->fmt_ctx);
        delete impl_; impl_ = nullptr;
        return false;
    }
    impl_->header_written = true;

    impl_->frame = av_frame_alloc();
    impl_->frame->format = AV_PIX_FMT_YUV420P;
    impl_->frame->width = width_;
    impl_->frame->height = height_;
    av_frame_get_buffer(impl_->frame, 0);

    impl_->pkt = av_packet_alloc();

    impl_->encode_running = true;
    impl_->encode_thread = std::thread(&Impl::encodeLoop, impl_, width_, height_);

    running_ = true;
    spdlog::info("CameraLogger: Started ({}x{}, MOV container, video+audio)", width_, height_);
    return true;
}

void CameraLogger::writeFrame(const CameraFrame& camera_frame) {
    if (!running_ || !impl_) return;

    if (camera_frame.width > 0 && camera_frame.height > 0 &&
        (camera_frame.width != width_ || camera_frame.height != height_)) {
        spdlog::info("CameraLogger: Resolution updated to {}x{}",
                     camera_frame.width, camera_frame.height);
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
            spdlog::warn("CameraLogger queue overflow: dropped oldest queued frame "
                         "(drops={}, depth={})",
                         drops, impl_->encode_queue.size());
        }
    }
}

void CameraLogger::writeVideoPacket(CameraNalPacket&& pkt) {
    if (!running_ || !impl_ || mode_ != Mode::Passthrough || pkt.data.empty()) return;

    bool dropped = impl_->nal_queue.try_push(std::move(pkt));
    updateMaxValue(impl_->max_queue_depth, impl_->nal_queue.size());
    if (dropped) {
        uint64_t drops = impl_->nal_packets_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (drops == 1 || drops % 100 == 0) {
            spdlog::warn("CameraLogger: mux 큐 오버플로 — 가장 오래된 패킷 폐기 "
                         "(drops={}, depth={})", drops, impl_->nal_queue.size());
        }
    }
}

void CameraLogger::setSourceSize(int width, int height) {
    if (!impl_ || width <= 0 || height <= 0) return;
    if (impl_->header_written) return;      // 이미 기록됨 — 변경 불가
    impl_->passthrough_width = width;
    impl_->passthrough_height = height;
    width_ = width;
    height_ = height;
}

void CameraLogger::writeAudioPacket(const CameraAudioPacket& pkt) {
    if (!running_ || !impl_ || !impl_->audio_stream || pkt.data.empty()) {
        if (running_ && impl_) {
            impl_->audio_packets_dropped.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    // If the source codec differs from what we declared, skip (caller should
    // have configured). We support pcm_mulaw passthrough for now.
    if (pkt.codec_id != impl_->audio_codec_id) {
        impl_->audio_packets_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 패스스루에서는 첫 키프레임 전까지 헤더가 아직 없다.
    if (!impl_->header_written) {
        impl_->audio_packets_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    AVPacket* avpkt = av_packet_alloc();
    if (!avpkt) return;

    avpkt->data = const_cast<uint8_t*>(pkt.data.data());
    avpkt->size = static_cast<int>(pkt.data.size());
    avpkt->stream_index = impl_->audio_stream->index;

    // pc_ts_rel (seconds) → audio_stream time_base (1/sample_rate)
    int64_t pts_units = static_cast<int64_t>(pkt.pc_ts_rel *
                                              static_cast<double>(impl_->audio_sample_rate));
    if (pts_units <= impl_->last_audio_pts) pts_units = impl_->last_audio_pts + 1;
    impl_->last_audio_pts = pts_units;
    avpkt->pts = pts_units;
    avpkt->dts = pts_units;

    // For pcm_mulaw / pcm_alaw: 1 byte per sample
    if (pkt.codec_id == AV_CODEC_ID_PCM_MULAW || pkt.codec_id == AV_CODEC_ID_PCM_ALAW) {
        avpkt->duration = static_cast<int64_t>(pkt.data.size());
    } else {
        avpkt->duration = 0;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->muxer_mutex);
        int ret = av_interleaved_write_frame(impl_->fmt_ctx, avpkt);
        if (ret < 0) {
            impl_->audio_packets_dropped.fetch_add(1, std::memory_order_relaxed);
        } else {
            impl_->audio_packets_written.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // Note: data buffer ownership: av_packet_alloc creates an empty packet,
    // we attached pkt.data without ref. av_interleaved_write_frame copies
    // when needed. av_packet_free will not free the user-attached data
    // because side_data ownership is separate.
    av_packet_free(&avpkt);
}

void CameraLogger::stop() {
    if (!running_ || !impl_) return;
    running_ = false;

    impl_->encode_running = false;
    if (impl_->encode_thread.joinable()) {
        impl_->encode_thread.join();
    }

    // 패스스루: mux 스레드가 남은 패킷을 모두 쓰고 종료하도록 한다.
    if (impl_->mux_running.load()) {
        impl_->mux_running = false;
        if (impl_->mux_thread.joinable()) impl_->mux_thread.join();
    }

    if (mode_ == Mode::Passthrough) {
        if (impl_->fmt_ctx && impl_->header_written) {
            std::lock_guard<std::mutex> lock(impl_->muxer_mutex);
            av_write_trailer(impl_->fmt_ctx);
        }
        if (impl_->fmt_ctx) {
            if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&impl_->fmt_ctx->pb);
            avformat_free_context(impl_->fmt_ctx);
        }
        spdlog::info("CameraLogger: Stopped (passthrough packets={}, drops={}, "
                     "max_queue={}, audio packets={}, audio drops={})",
                     impl_->nal_packets_written.load(std::memory_order_relaxed),
                     impl_->nal_packets_dropped.load(std::memory_order_relaxed),
                     impl_->max_queue_depth.load(std::memory_order_relaxed),
                     impl_->audio_packets_written.load(std::memory_order_relaxed),
                     impl_->audio_packets_dropped.load(std::memory_order_relaxed));
        delete impl_; impl_ = nullptr;
        return;
    }

    // Flush video encoder
    if (impl_->codec_ctx && impl_->pkt && impl_->fmt_ctx) {
        avcodec_send_frame(impl_->codec_ctx, nullptr);
        while (true) {
            int ret = avcodec_receive_packet(impl_->codec_ctx, impl_->pkt);
            if (ret == AVERROR_EOF || ret < 0) break;
            av_packet_rescale_ts(impl_->pkt, impl_->codec_ctx->time_base,
                                 impl_->video_stream->time_base);
            impl_->pkt->stream_index = impl_->video_stream->index;
            {
                std::lock_guard<std::mutex> lock(impl_->muxer_mutex);
                av_interleaved_write_frame(impl_->fmt_ctx, impl_->pkt);
            }
            av_packet_unref(impl_->pkt);
        }

        std::lock_guard<std::mutex> lock(impl_->muxer_mutex);
        av_write_trailer(impl_->fmt_ctx);
    }

    impl_->freeBandContexts();
    if (impl_->sws_ctx)   sws_freeContext(impl_->sws_ctx);
    if (impl_->pkt)       av_packet_free(&impl_->pkt);
    if (impl_->frame)     av_frame_free(&impl_->frame);
    if (impl_->codec_ctx) avcodec_free_context(&impl_->codec_ctx);
    if (impl_->fmt_ctx) {
        if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&impl_->fmt_ctx->pb);
        avformat_free_context(impl_->fmt_ctx);
    }

    spdlog::info("CameraLogger: Stopped (video frames={}, max_queue={}, "
                  "queue_drops={}, audio packets={}, audio drops={})",
                  impl_->pts_counter,
                  impl_->max_queue_depth.load(std::memory_order_relaxed),
                  impl_->dropped_frames.load(std::memory_order_relaxed),
                  impl_->audio_packets_written.load(std::memory_order_relaxed),
                  impl_->audio_packets_dropped.load(std::memory_order_relaxed));

    delete impl_;
    impl_ = nullptr;
}

} // namespace msl
