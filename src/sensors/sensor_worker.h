#pragma once

#include "core/event_bus.h"
#include "core/logging.h"
#include "core/clock.h"
#include <thread>
#include <atomic>
#include <functional>
#include <spdlog/spdlog.h>

namespace msl {

/// Abstract base class for sensor worker threads.
/// Mirrors Python BaseSensorWorker(QThread) pattern.
class ISensorWorker {
public:
    explicit ISensorWorker(EventBus& bus) : event_bus_(bus) {}
    virtual ~ISensorWorker() { stop(); }

    ISensorWorker(const ISensorWorker&) = delete;
    ISensorWorker& operator=(const ISensorWorker&) = delete;

    /// Start the worker thread.
    void start() {
        if (running_) return;
        running_ = true;
        connect_requested_ = true;
        thread_ = std::thread([this]() { run(); });
    }

    /// Stop the worker thread (blocks up to 5 seconds).
    void stop() {
        running_ = false;
        disconnect_requested_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        connected_ = false;
    }

    /// Request connection (thread-safe).
    void requestConnect() { connect_requested_ = true; }

    /// Request disconnection (thread-safe).
    void requestDisconnect() { disconnect_requested_ = true; }

    /// Set recording state.
    void setRecording(bool active, double start_time = 0.0) {
        rec_start_time_ = start_time;
        recording_ = active;
    }

    /// Get time elapsed since recording start.
    double getRelativeTime() const {
        return Clock::now() - rec_start_time_.load();
    }

    /// Check connection state.
    bool isConnected() const { return connected_; }

    /// Route this worker's log output to a channel folder (lidar/radar/...).
    /// MainWindow sets this right after construction, before start().
    void setLogChannel(log::Channel ch) { log_ch_ = ch; }

    /// Logger of this worker's channel. Never null.
    const std::shared_ptr<spdlog::logger>& L() const { return log::get(log_ch_); }

    /// Get total frames received.
    uint64_t frameCount() const { return frame_count_; }

    // Callbacks posted to EventBus (set by MainWindow)
    std::function<void(bool)> on_connection_changed;
    std::function<void(const std::string&)> on_error;

protected:
    EventBus& event_bus_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> connect_requested_{false};
    std::atomic<bool> disconnect_requested_{false};
    std::atomic<bool> recording_{false};
    std::atomic<double> rec_start_time_{0.0};
    std::atomic<uint64_t> frame_count_{0};
    log::Channel log_ch_ = log::Channel::Ui;

    /// Subclass implements the polling loop.
    /// Called when connect is requested. Should loop until running_ is false
    /// or disconnect_requested_ is true.
    virtual void pollLoop() = 0;

    /// Called before pollLoop for any initialization.
    virtual bool onConnect() { return true; }

    /// Called after pollLoop exits.
    virtual void onDisconnect() {}

    /// Post a connection state change to the main thread.
    void notifyConnectionChanged(bool state) {
        connected_ = state;
        if (on_connection_changed) {
            auto cb = on_connection_changed;
            event_bus_.post([cb, state]() { cb(state); });
        }
    }

    /// Post an error message to the main thread.
    void notifyError(const std::string& msg) {
        L()->error("{}", msg);
        if (on_error) {
            auto cb = on_error;
            event_bus_.post([cb, msg]() { cb(msg); });
        }
    }

private:
    std::thread thread_;

    void run() {
        while (running_) {
            if (connect_requested_) {
                connect_requested_ = false;
                disconnect_requested_ = false;

                if (onConnect()) {
                    notifyConnectionChanged(true);
                    pollLoop();
                    onDisconnect();
                    notifyConnectionChanged(false);
                } else {
                    notifyError("Failed to connect sensor");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

} // namespace msl
