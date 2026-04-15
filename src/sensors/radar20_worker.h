#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>

namespace msl {

/// BSR20 Radar worker: polling-based via byda_c_api.h
class Radar20Worker : public ISensorWorker {
public:
    Radar20Worker(EventBus& bus, const std::string& ip, int port)
        : ISensorWorker(bus), ip_(ip), port_(port) {}

    std::function<void(const RadarScanData&)> on_scan_ready;

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    std::string ip_;
    int port_;
    void* handle_ = nullptr;  // BydaRadarHandle (opaque)
};

} // namespace msl
