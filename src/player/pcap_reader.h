#pragma once

#include "core/sensor_data.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace msl {

/// Reads lidar pcap files and provides frame-by-frame XYZ point clouds.
/// LAZY-LOAD: open() only indexes scan boundaries (offsets + timestamps).
/// getPointCloud() decodes the requested scan on demand. This keeps memory
/// usage low even for very long recordings (5+ minutes).
///
/// Implementation (Impl) is hidden in the .cpp to avoid leaking Ouster SDK
/// headers to downstream consumers that don't link against the SDK directly.
class PcapReader {
public:
    PcapReader();
    ~PcapReader();

    PcapReader(const PcapReader&) = delete;
    PcapReader& operator=(const PcapReader&) = delete;

    /// Open pcap + metadata. Indexes scans without storing decoded XYZ.
    bool open(const std::string& pcap_path, const std::string& metadata_path);

    int frameCount() const;
    const std::vector<double>& timestamps() const;

    /// Get XYZ point cloud for frame index. Decodes on demand.
    bool getPointCloud(int frame_idx, std::vector<float>& xyz_out, int& num_points);

    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace msl
