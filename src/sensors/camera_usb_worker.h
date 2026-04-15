#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>

namespace msl {

/// USB camera worker via CameraCapture (ccap) library.
class CameraUsbWorker : public ISensorWorker {
public:
    CameraUsbWorker(EventBus& bus, int device_index, int width, int height)
        : ISensorWorker(bus), device_index_(device_index),
          width_(width), height_(height) {}

    std::function<void(const CameraFrame&)> on_frame_ready;

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    int device_index_;
    int width_;
    int height_;

    // ccap provider handle (opaque)
    void* provider_ = nullptr;
};

} // namespace msl
