#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include <functional>
#include <string>

namespace msl {

/// Ouster LiDAR + IMU worker via ouster-sdk.
/// Provides both lidar scan data and IMU data.
/// Raw packets are captured for pcap recording.
class OusterWorker : public ISensorWorker {
public:
    OusterWorker(EventBus& bus, const std::string& ip, int lidar_port, int imu_port)
        : ISensorWorker(bus), ip_(ip), lidar_port_(lidar_port), imu_port_(imu_port) {}

    std::function<void(const LidarScanData&)> on_lidar_scan_ready;
    std::function<void(const ImuData&)> on_imu_data_ready;

    /// Called for every raw lidar packet (for pcap recording).
    std::function<void(const std::vector<uint8_t>& raw, double pc_ts_rel, bool scan_complete)> on_raw_lidar_packet;

    /// Get sensor metadata JSON string (available after connection).
    std::string getMetadataJson() const;

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    std::string ip_;
    int lidar_port_;
    int imu_port_;

    // Ouster SDK internals (forward-declared, implemented in .cpp)
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace msl
