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
    OusterWorker(EventBus& bus, const std::string& ip, int lidar_port, int imu_port,
                 bool multicast_enabled = false,
                 const std::string& multicast_dest = "",
                 const std::string& mtp_dest = "",
                 bool mtp_main = false)
        : ISensorWorker(bus), ip_(ip), lidar_port_(lidar_port), imu_port_(imu_port),
          multicast_enabled_(multicast_enabled),
          multicast_dest_(multicast_dest),
          mtp_dest_(mtp_dest),
          mtp_main_(mtp_main) {}

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

    // Multicast config (for coexistence with ROS primary subscriber)
    bool        multicast_enabled_;
    std::string multicast_dest_;   // e.g. "239.201.201.201"
    std::string mtp_dest_;         // NIC IP to bind (empty = auto)
    bool        mtp_main_;         // true = configure sensor; false = passive listener

    // Ouster SDK internals (forward-declared, implemented in .cpp)
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace msl
