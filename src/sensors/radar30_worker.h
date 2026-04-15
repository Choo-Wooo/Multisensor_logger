#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include "core/thread_safe_queue.h"
#include <functional>
#include <string>

namespace msl {

/// BSR30 Radar worker: callback-driven via Bsr30Sdk.h
class Radar30Worker : public ISensorWorker {
public:
    Radar30Worker(EventBus& bus, const std::string& ip, int tcp_port, int udp_port)
        : ISensorWorker(bus), ip_(ip), tcp_port_(tcp_port), udp_port_(udp_port) {}

    std::function<void(const RadarScanData&)> on_scan_ready;

    /// Called from static C callback to push data into internal queue.
    void pushScan(RadarScanData scan);

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

    // Static callback adapter (BSR30 SDK uses C function pointer)
    static void frameCallback(const void* frame, void* user_data);
};

} // namespace msl
