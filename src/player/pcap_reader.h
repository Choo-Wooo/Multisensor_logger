#pragma once

#include "core/sensor_data.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace msl {

/// Reads lidar pcap files and provides frame-by-frame XYZ point clouds.
/// Uses ouster SDK ScanBatcher + XYZLut for decoding.
class PcapReader {
public:
    PcapReader() = default;
    ~PcapReader();

    /// Open pcap + metadata. Preloads all scans into memory.
    bool open(const std::string& pcap_path, const std::string& metadata_path);

    int frameCount() const { return static_cast<int>(scans_.size()); }
    const std::vector<double>& timestamps() const { return timestamps_; }

    /// Get XYZ point cloud for frame index.
    bool getPointCloud(int frame_idx, std::vector<float>& xyz_out, int& num_points);

    void close();

private:
    struct ScanData {
        std::vector<float> xyz;  // Flattened N*3
        int num_points = 0;
    };

    std::vector<ScanData> scans_;
    std::vector<double> timestamps_;
};

} // namespace msl
