#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>

namespace msl {

/// RTSP camera worker: live555 event loop + FFmpeg H.264 decoding.
class CameraRtspWorker : public ISensorWorker {
public:
    CameraRtspWorker(EventBus& bus, const std::string& url, int width, int height)
        : ISensorWorker(bus), url_(url), width_(width), height_(height) {}

    std::function<void(const CameraFrame&)> on_frame_ready;

    // live555 + FFmpeg internals (public for static response handlers in .cpp)
    struct Impl;
    Impl* impl_ = nullptr;

    /// Signal live555 event loop to exit.
    void signalEventLoopExit();

    /// Stop with live555 event loop signal.
    void stopRtsp() {
        if (impl_) signalEventLoopExit();
        stop();
    }

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    std::string url_;
    int width_;
    int height_;
};

} // namespace msl
