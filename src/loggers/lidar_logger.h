#pragma once

#include "core/thread_safe_queue.h"
#include "core/sensor_data.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>

namespace msl {

/// Pcap index record for seek support
#pragma pack(push, 1)
struct PcapIndexRecord {
    double  pc_ts_rel;   // 8B
    int64_t file_offset; // 8B
};
#pragma pack(pop)

/// Raw lidar packet for pcap recording
struct LidarRawPacket {
    std::vector<uint8_t> data;
    double pc_ts_rel = 0.0;
    bool is_scan_complete = false;  // True when this packet completes a scan
};

/// Lidar logger: writes raw UDP packets as pcap + .pcap.idx sidecar.
/// Receives individual packets (not complete scans).
class LidarLogger {
public:
    LidarLogger(const std::string& pcap_path, const std::string& idx_path,
                const std::string& sensor_ip = "192.168.172.129", int lidar_port = 7502)
        : pcap_path_(pcap_path), idx_path_(idx_path),
          sensor_ip_(sensor_ip), lidar_port_(lidar_port) {}
    ~LidarLogger() { stop(); }

    void enqueue(LidarRawPacket pkt) {
        queue_.try_push(std::move(pkt));
    }

    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    std::string pcap_path_;
    std::string idx_path_;
    std::string sensor_ip_;
    int lidar_port_;
    ThreadSafeQueue<LidarRawPacket, 1000> queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::ofstream pcap_file_;
    int64_t file_offset_ = 0;
    std::vector<PcapIndexRecord> index_records_;

    void writeLoop();
    void writePcapGlobalHeader();
    void writePcapPacket(const uint8_t* data, size_t size, double timestamp);
};

} // namespace msl
