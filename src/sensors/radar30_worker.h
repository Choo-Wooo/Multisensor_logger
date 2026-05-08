#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include "core/thread_safe_queue.h"
#include <functional>
#include <string>
#include <atomic>

namespace msl {

/// BSR30 Radar worker: callback-driven via Bsr30Sdk.h
///
/// Two-phase lifecycle (split for explicit user control):
///   Connect phase (onConnect):
///     1. set callback
///     2. bsr30_connect()  — TCP/UDP socket only
///   Start phase (requestStart → doStart in pollLoop):
///     3. poll bsr30_radar_start() until success (up to 25s; TCP handshake
///        completes asynchronously after connect)
///
/// Connect and Start are intentionally separated so the user (or test
/// scripts) can issue them independently from the UI. This mirrors the
/// BSR30 reference sample, which uses two separate buttons.
///
/// Once streaming, a watchdog inside pollLoop() detects frame loss
/// (no callback for >5s) and triggers stop+disconnect+reconnect+restart.
class Radar30Worker : public ISensorWorker {
public:
    Radar30Worker(EventBus& bus, const std::string& ip, int tcp_port, int udp_port)
        : ISensorWorker(bus), ip_(ip), tcp_port_(tcp_port), udp_port_(udp_port) {}

    std::function<void(const RadarScanData&)> on_scan_ready;
    /// Called when watchdog enters/exits a reconnect attempt.
    /// (true = reconnecting, false = either reconnected OK or gave up)
    std::function<void(bool)> on_reconnecting;
    /// Called when the streaming state flips (after doStart succeeds, or
    /// after doStop completes). Posted to the main thread via EventBus.
    std::function<void(bool)> on_streaming_changed;

    /// Called from static C callback to push data into internal queue.
    void pushScan(RadarScanData scan);

    /// Non-blocking: ask the worker thread to call bsr30_radar_start() and
    /// begin streaming. The actual start (which can take up to 25s while
    /// polling) runs inside pollLoop, never on the caller's (UI) thread.
    /// Safe to call multiple times; ignored when already streaming.
    void requestStart();

    /// Non-blocking: ask the worker thread to call bsr30_radar_stop() and
    /// halt streaming, without disconnecting. Safe to call multiple times;
    /// ignored when not streaming.
    void requestStop();

    /// True between successful doStart() and the next doStop()/disconnect.
    bool isStreaming() const { return streaming_.load(std::memory_order_relaxed); }

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    std::string ip_;
    int tcp_port_;
    int udp_port_;

    // Internal queue to marshal callback data to worker thread
    ThreadSafeQueue<RadarScanData, 100> callback_queue_;

    // Watchdog: timestamp of last frame received (steady_clock ms).
    std::atomic<long long> last_frame_ms_{0};

    // Streaming control flags. The UI thread sets *_request_; the worker
    // thread reads them inside pollLoop and calls doStart/doStop.
    std::atomic<bool> start_request_{false};
    std::atomic<bool> stop_request_{false};
    std::atomic<bool> streaming_{false};

    /// Run on the worker thread. Polls bsr30_radar_start() up to 25s.
    /// On success: sets streaming_=true, primes last_frame_ms_, fires
    /// on_streaming_changed(true). On failure/cancel: returns false and
    /// leaves streaming_=false.
    bool doStart();

    /// Run on the worker thread. bsr30_radar_stop(), clears streaming_,
    /// fires on_streaming_changed(false).
    void doStop();

    /// Best-effort reconnect — cycles bsr30_radar_stop, bsr30_disconnect,
    /// bsr30_connect, then polls bsr30_radar_start() (same logic as
    /// onConnect+doStart without re-registering the frame callback).
    /// Returns true if streaming resumed; false if cancelled or gave up.
    bool reconnectInternal();

    /// Steady-clock now() in milliseconds.
    static long long nowMs();

    /// Notify host (main thread) that we're entering/leaving reconnect.
    void notifyReconnecting(bool flag);

    /// Notify host (main thread) that streaming_ flipped.
    void notifyStreamingChanged(bool flag);

    // Static callback adapter (BSR30 SDK uses C function pointer)
    static void frameCallback(const void* frame, void* user_data);
};

} // namespace msl
