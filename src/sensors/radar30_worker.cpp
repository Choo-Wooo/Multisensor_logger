#include "radar30_worker.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>

// BSR30 SDK
#include <Bsr30Sdk.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace msl {

// BSR30 callback has no user_data parameter, so use a file-scope static pointer.
static Radar30Worker* g_radar30_instance = nullptr;

// Tunables (match the reference sample)
static constexpr int  CONNECT_DEADLINE_MS = 25000; ///< max wait for TCP ready
static constexpr int  START_RETRY_MS      = 500;   ///< bsr30_radar_start poll
static constexpr int  RETRY_DELAY_MS      = 1000;  ///< between connect attempts
static constexpr long long WATCHDOG_MS    = 5000;  ///< frame-loss timeout

static_assert(sizeof(bsr30_track_t) == 40, "Unexpected BSR30 track size");

#pragma pack(push, 1)
// Mirrors the BSR30 SDK's bsr30_track_t layout (40 bytes, packed).
// Used only for diagnostic logging — re-uses snake_case so log lines
// match the verification tool's field naming convention.
struct Radar30VerificationTrackView {
    uint8_t  id;
    uint8_t  bankid;            // 0 = LRR/Master, 1 = SRR/Slave
    uint16_t pw;
    uint32_t sp_flag;
    float    reserved0;
    float    init_pos_vy_x1kph;
    float    x_pos_pred_1xM;
    float    y_pos_pred_1xM;
    float    x_vel_pred_1xKph;
    float    y_vel_pred_1xKph;
    int8_t   lane_num;
    uint8_t  vehicle_type;
    uint8_t  reserved2;
    int8_t   init_lane_num;
    uint8_t  padding[4];
};
#pragma pack(pop)

#ifdef _WIN32
void logSdkBindingDiagnostics() {
    bsr30_sdk_version_t version{};
    bsr30_sdk_get_version(&version);

    char module_path[MAX_PATH] = {};
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&bsr30_sdk_get_version),
                           &module) &&
        module) {
        const DWORD copied = GetModuleFileNameA(
            module, module_path, static_cast<DWORD>(sizeof(module_path)));
        if (copied == 0 || copied >= sizeof(module_path)) {
            module_path[0] = '\0';
        }
    }

    spdlog::info(
        "Radar30 SDK binding: version={}.{}.{} name='{}' manufacturer='{}'",
        version.major,
        version.minor,
        version.patch,
        version.name ? version.name : "<null>",
        version.manufacturer ? version.manufacturer : "<null>");
    spdlog::info(
        "Radar30 SDK binding: dll_path={}",
        module_path[0] != '\0' ? module_path : "<unknown>");
    spdlog::info(
        "Radar30 SDK binding: sizeof(frame)={} sizeof(track)={} BSR30_TRACK_COUNT={} "
        "frame_offsets(sequence={}, timestamp={}, sys_frame_num={}, tracks={}) "
        "track_offsets(id={}, bankid={}, pw={}, spFlag={}, initPosVY_x1kph={}, "
        "xPos_pred_1xM={}, yPos_pred_1xM={}, xVel_pred_1xKph={}, yVel_pred_1xKph={}, "
        "laneNum={}, vehicleType={}, initLaneNum={})",
        sizeof(bsr30_frame_t),
        sizeof(bsr30_track_t),
        BSR30_TRACK_COUNT,
        offsetof(bsr30_frame_t, sequence),
        offsetof(bsr30_frame_t, timestamp),
        offsetof(bsr30_frame_t, sys_frame_num),
        offsetof(bsr30_frame_t, tracks),
        offsetof(bsr30_track_t, id),
        offsetof(bsr30_track_t, bankid),
        offsetof(bsr30_track_t, pw),
        offsetof(bsr30_track_t, spFlag),
        offsetof(bsr30_track_t, initPosVY_x1kph),
        offsetof(bsr30_track_t, xPos_pred_1xM),
        offsetof(bsr30_track_t, yPos_pred_1xM),
        offsetof(bsr30_track_t, xVel_pred_1xKph),
        offsetof(bsr30_track_t, yVel_pred_1xKph),
        offsetof(bsr30_track_t, laneNum),
        offsetof(bsr30_track_t, vehicleType),
        offsetof(bsr30_track_t, initLaneNum));
}
#endif

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
long long Radar30Worker::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void staticFrameCallback(const bsr30_frame_t* frame) {
    if (!g_radar30_instance) {
        spdlog::warn("Radar30: frame callback received but instance is null");
        return;
    }
    if (!frame) {
        spdlog::warn("Radar30: frame callback received null pointer");
        return;
    }

    int active = 0;
    for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
        const auto& t = frame->tracks[i];
        if (t.id != 0 || t.pw > 0) {
            ++active;
        }
    }

    if (active > 0 && spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug(
            "Radar30: [track-frame] seq={} timestamp_ms={} active_tracks={} source=bsr30_sdk_callback",
            frame->sequence, frame->timestamp, active);

        for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
            const auto& t = frame->tracks[i];
            if (t.id == 0 && t.pw == 0) continue;

            // Reinterpret the SDK track memory using the verification tool's
            // network_radar_protocol field names so we can compare logs 1:1.
            Radar30VerificationTrackView proto{};
            static_assert(sizeof(proto) == sizeof(t), "BSR30 track layout mismatch");
            std::memcpy(&proto, &t, sizeof(proto));

            // The current BEV intentionally rotates the radar view 90 degrees:
            // horizontal axis = forward (+y), vertical axis = -lateral (-x).
            const float bev_x = proto.y_pos_pred_1xM;
            const float bev_y = -proto.x_pos_pred_1xM;

            spdlog::debug(
                "Radar30: [track] seq={} slot={} id={} bankid={} pw={} spFlag=0x{:08X} "
                "initPosVY_x1kph={:.3f} xPos_pred_1xM={:.3f} yPos_pred_1xM={:.3f} "
                "xVel_pred_1xKph={:.3f} yVel_pred_1xKph={:.3f} "
                "laneNum={} initLaneNum={} vehicleType={} bev_rot90=({:.3f},{:.3f})",
                frame->sequence, i, (int)proto.id, (int)proto.bankid,
                (int)proto.pw, proto.sp_flag,
                proto.init_pos_vy_x1kph, proto.x_pos_pred_1xM, proto.y_pos_pred_1xM,
                proto.x_vel_pred_1xKph, proto.y_vel_pred_1xKph,
                (int)proto.lane_num, (int)proto.init_lane_num, (int)proto.vehicle_type,
                bev_x, bev_y);
        }
    }

    RadarScanData scan;
    scan.scan_index = frame->sequence;
    scan.timestamp  = frame->timestamp;

    for (int i = 0; i < BSR30_TRACK_COUNT; ++i) {
        const auto& t = frame->tracks[i];
        // Keep the existing live-path behavior unchanged for now:
        // id == 0 is treated as an empty slot by the current logger/view path.
        if (t.id == 0) continue;

        RadarTrack track;
        track.id = t.id;
        // BSR30 axis convention (matches verification tool's protocol):
        //   xPos_pred_1xM = lateral (좌우, +right)
        //   yPos_pred_1xM = forward (전방 거리)
        // The `_1x` / `1xKph` suffixes are scale hints (x1 m, x1 kph) added
        // by the BSR30 SDK protocol revision; values are still plain meters
        // and kph. Both platforms now use the same field names.
        track.x_pos = t.xPos_pred_1xM;
        track.y_pos = t.yPos_pred_1xM;
        track.x_vel = t.xVel_pred_1xKph;
        track.y_vel = t.yVel_pred_1xKph;
        track.type = t.vehicleType;
        // bankid (0 = LRR/master, 1 = SRR/slave) is intentionally not surfaced
        // through RadarTrack here to keep the BSR20/30 schema uniform; it's
        // logged at debug level via Radar30VerificationTrackView for now.
        track.status = 0;
        scan.tracks.push_back(track);
    }

    g_radar30_instance->pushScan(std::move(scan));
}

// ---------------------------------------------------------------------------
// Worker methods
// ---------------------------------------------------------------------------
void Radar30Worker::pushScan(RadarScanData scan) {
    // Watchdog freshness: record arrival time of the callback, not pop time.
    last_frame_ms_.store(nowMs(), std::memory_order_relaxed);

    bool dropped_old = callback_queue_.try_push(std::move(scan));
    if (dropped_old) {
        spdlog::warn("Radar30: callback queue full, dropped oldest frame");
    }
}

void Radar30Worker::notifyReconnecting(bool flag) {
    if (on_reconnecting) {
        auto cb = on_reconnecting;
        event_bus_.post([cb, flag]() { cb(flag); });
    }
}

void Radar30Worker::notifyStreamingChanged(bool flag) {
    if (on_streaming_changed) {
        auto cb = on_streaming_changed;
        event_bus_.post([cb, flag]() { cb(flag); });
    }
}

void Radar30Worker::requestStart() {
    if (streaming_.load(std::memory_order_relaxed)) {
        spdlog::info("Radar30: requestStart ignored, already streaming");
        return;
    }
    start_request_.store(true, std::memory_order_release);
    spdlog::info("Radar30: Start requested");
}

void Radar30Worker::requestStop() {
    if (!streaming_.load(std::memory_order_relaxed)) {
        spdlog::info("Radar30: requestStop ignored, not streaming");
        return;
    }
    stop_request_.store(true, std::memory_order_release);
    spdlog::info("Radar30: Stop requested");
}

bool Radar30Worker::onConnect() {
    g_radar30_instance = this;
    bsr30_set_radar_frame_callback(staticFrameCallback);

#ifdef _WIN32
    logSdkBindingDiagnostics();
#endif

    spdlog::info("Radar30: Connecting to {} (TCP:{} UDP:{})",
                 ip_, tcp_port_, udp_port_);

    if (!bsr30_connect(ip_.c_str(), tcp_port_, udp_port_)) {
        notifyError("Radar30: Connect failed to " + ip_);
        g_radar30_instance = nullptr;
        return false;
    }

    // Connect-only: the user must explicitly press "Start Radar" to begin
    // streaming. Mirrors the BSR30 reference sample's two-button flow.
    spdlog::info("Radar30: Connected. Awaiting Start command.");
    return true;
}

bool Radar30Worker::doStart() {
    spdlog::info("Radar30: doStart, polling bsr30_radar_start()...");

    int elapsed = 0;
    while (running_ && !disconnect_requested_ &&
           !stop_request_.load(std::memory_order_acquire) &&
           elapsed < CONNECT_DEADLINE_MS) {
        if (bsr30_radar_start()) {
            last_frame_ms_.store(nowMs(), std::memory_order_relaxed);
            streaming_.store(true, std::memory_order_release);
            notifyStreamingChanged(true);
            spdlog::info("Radar30: Streaming started on {}:{}/{}",
                         ip_, tcp_port_, udp_port_);
            return true;
        }
        int remaining = (CONNECT_DEADLINE_MS - elapsed) / 1000;
        spdlog::info("Radar30: Waiting TCP ready... ({}s left)", remaining);
        std::this_thread::sleep_for(std::chrono::milliseconds(START_RETRY_MS));
        elapsed += START_RETRY_MS;
    }

    if (stop_request_.load(std::memory_order_acquire)) {
        spdlog::info("Radar30: doStart cancelled by stop request");
    } else {
        notifyError("Radar30: start timeout after 25s");
    }
    return false;
}

void Radar30Worker::doStop() {
    spdlog::info("Radar30: doStop, bsr30_radar_stop()");
    bsr30_radar_stop();
    streaming_.store(false, std::memory_order_release);
    notifyStreamingChanged(false);
}

bool Radar30Worker::reconnectInternal() {
    // Stop and tear down current connection.
    bsr30_radar_stop();
    bsr30_disconnect();
    // Streaming is logically interrupted while we cycle the connection;
    // the consuming code (UI) is informed via on_reconnecting, so we
    // intentionally keep streaming_=true here so the watchdog gates
    // resume correctly when reconnect succeeds.

    // Optional brief delay so the device gets a chance to settle.
    std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));

    // Re-establish: same as onConnect's connect+poll-start, but the
    // callback is already registered, so we don't touch it.
    while (running_ && !disconnect_requested_ &&
           !stop_request_.load(std::memory_order_acquire)) {
        spdlog::info("Radar30: [reconnect] connecting to {}...", ip_);
        if (!bsr30_connect(ip_.c_str(), tcp_port_, udp_port_)) {
            spdlog::warn("Radar30: [reconnect] connect failed, retry in {}s",
                         RETRY_DELAY_MS / 1000);
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
            continue;
        }

        int elapsed = 0;
        bool started = false;
        while (running_ && !disconnect_requested_ &&
               !stop_request_.load(std::memory_order_acquire) &&
               elapsed < CONNECT_DEADLINE_MS) {
            if (bsr30_radar_start()) { started = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(START_RETRY_MS));
            elapsed += START_RETRY_MS;
        }

        if (started) {
            last_frame_ms_.store(nowMs(), std::memory_order_relaxed);
            spdlog::info("Radar30: [reconnect] streaming resumed");
            return true;
        }

        spdlog::warn("Radar30: [reconnect] start timeout, cycling...");
        bsr30_disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
    }

    // Cancelled: either by stop request or full disconnect. The watchdog
    // path will exit pollLoop; ensure we leave streaming_ false so the UI
    // doesn't think we're still streaming.
    streaming_.store(false, std::memory_order_release);
    notifyStreamingChanged(false);
    return false;
}

void Radar30Worker::pollLoop() {
    while (running_ && !disconnect_requested_) {
        // 1. Process start request from UI thread.
        if (start_request_.exchange(false, std::memory_order_acq_rel)) {
            if (!streaming_.load(std::memory_order_relaxed)) {
                doStart();
                // doStart may take up to 25s; re-check loop conditions.
                continue;
            }
        }

        // 2. Process stop request from UI thread.
        if (stop_request_.exchange(false, std::memory_order_acq_rel)) {
            if (streaming_.load(std::memory_order_relaxed)) {
                doStop();
            }
        }

        // 3. Drain queued scans (only meaningful while streaming, but
        // draining when not streaming is harmless).
        RadarScanData scan;
        while (callback_queue_.try_pop(scan, std::chrono::milliseconds(0))) {
            if (recording_) {
                scan.pc_ts_rel = getRelativeTime();
            }
            frame_count_++;

            if (on_scan_ready) {
                auto cb = on_scan_ready;
                event_bus_.post([cb, scan]() { cb(scan); });
            }
        }

        // 4. Watchdog: only active while streaming.
        if (streaming_.load(std::memory_order_relaxed)) {
            long long elapsed = nowMs() - last_frame_ms_.load(std::memory_order_relaxed);
            if (elapsed > WATCHDOG_MS) {
                spdlog::warn("Radar30: No frame for {}ms, reconnecting...", elapsed);

                notifyReconnecting(true);
                bool ok = reconnectInternal();
                notifyReconnecting(false);

                if (!ok) {
                    // reconnectInternal() left streaming_=false on cancel.
                    spdlog::error("Radar30: Reconnect cancelled, exiting pollLoop");
                    break;
                }
                // Successful reconnect: streaming_ stays true, continue.
            }
        }

        // Small sleep so we're not busy-waiting when the queue is empty.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Radar30Worker::onDisconnect() {
    // If streaming, halt cleanly first so the UI sees the streaming flag drop.
    if (streaming_.load(std::memory_order_relaxed)) {
        bsr30_radar_stop();
        streaming_.store(false, std::memory_order_release);
        notifyStreamingChanged(false);
    } else {
        // Idempotent: safe even if not streaming.
        bsr30_radar_stop();
    }
    bsr30_disconnect();
    g_radar30_instance = nullptr;
    callback_queue_.clear();

    // Clear any pending requests so a future reconnect starts fresh.
    start_request_.store(false, std::memory_order_relaxed);
    stop_request_.store(false, std::memory_order_relaxed);

    spdlog::info("Radar30: Disconnected");
}

} // namespace msl
