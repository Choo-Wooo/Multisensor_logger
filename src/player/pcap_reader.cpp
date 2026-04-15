#include "pcap_reader.h"
#include "loggers/lidar_logger.h"  // PcapIndexRecord
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstring>
#include <filesystem>

// Ouster SDK
#include <ouster/types.h>
#include <ouster/xyzlut.h>
#include <ouster/lidar_scan.h>
#include <ouster/packet.h>

namespace fs = std::filesystem;
namespace osdk_core = ouster::sdk::core;

namespace msl {

PcapReader::~PcapReader() { close(); }

bool PcapReader::open(const std::string& pcap_path, const std::string& metadata_path) {
    close();

    // Load sensor metadata
    if (!fs::exists(metadata_path)) {
        spdlog::error("PcapReader: Metadata not found: {}", metadata_path);
        return false;
    }

    std::ifstream meta_file(metadata_path);
    std::string meta_json((std::istreambuf_iterator<char>(meta_file)),
                           std::istreambuf_iterator<char>());
    meta_file.close();

    if (meta_json.empty()) {
        spdlog::error("PcapReader: Empty metadata file");
        return false;
    }

    osdk_core::SensorInfo info(meta_json);
    auto xyz_lut = osdk_core::make_xyz_lut(info, true);
    osdk_core::ScanBatcher batcher(info);
    osdk_core::LidarScan scan(info);

    // Load pcap index
    std::string idx_path = pcap_path + ".idx";
    std::vector<PcapIndexRecord> index;
    if (fs::exists(idx_path)) {
        std::ifstream idx_file(idx_path, std::ios::binary);
        uint32_t count = 0;
        idx_file.read(reinterpret_cast<char*>(&count), 4);
        index.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            idx_file.read(reinterpret_cast<char*>(&index[i]), sizeof(PcapIndexRecord));
        }
    }

    // Read pcap file
    std::ifstream pcap_file(pcap_path, std::ios::binary);
    if (!pcap_file.is_open()) {
        spdlog::error("PcapReader: Failed to open {}", pcap_path);
        return false;
    }

    // Skip pcap global header (24 bytes)
    pcap_file.seekg(24);

    // Read all packets and batch into scans
    struct PcapRecordHeader {
        uint32_t ts_sec;
        uint32_t ts_usec;
        uint32_t incl_len;
        uint32_t orig_len;
    };

    int scan_count = 0;
    double current_ts = 0.0;

    while (pcap_file.good() && !pcap_file.eof()) {
        PcapRecordHeader hdr;
        pcap_file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!pcap_file.good()) break;

        if (hdr.incl_len == 0 || hdr.incl_len > 100000) break;  // Sanity check

        std::vector<uint8_t> buf(hdr.incl_len);
        pcap_file.read(reinterpret_cast<char*>(buf.data()), hdr.incl_len);
        if (!pcap_file.good() && !pcap_file.eof()) break;

        current_ts = hdr.ts_sec + hdr.ts_usec / 1000000.0;

        // Strip Ethernet(14) + IP(20) + UDP(8) = 42 byte header if present
        try {
            const uint8_t* payload = buf.data();
            size_t payload_size = buf.size();

            // Check if this looks like an Ethernet frame (EtherType 0x0800 at offset 12)
            if (buf.size() > 42 && buf[12] == 0x08 && buf[13] == 0x00) {
                payload += 42;
                payload_size -= 42;
            }

            osdk_core::LidarPacket pkt(static_cast<int>(payload_size));
            std::memcpy(pkt.buf.data(), payload, payload_size);
            pkt.host_timestamp = static_cast<uint64_t>(current_ts * 1e9);

            // Try to batch as lidar packet
            if (batcher(pkt, scan)) {
                // Full scan ready
                auto cloud = osdk_core::cartesian(scan, xyz_lut);
                int n = static_cast<int>(cloud.rows());

                ScanData sd;
                sd.num_points = n;
                sd.xyz.resize(n * 3);
                for (int i = 0; i < n; ++i) {
                    sd.xyz[i * 3 + 0] = static_cast<float>(cloud(i, 0));
                    sd.xyz[i * 3 + 1] = static_cast<float>(cloud(i, 1));
                    sd.xyz[i * 3 + 2] = static_cast<float>(cloud(i, 2));
                }

                // Use index timestamp if available, otherwise pcap timestamp
                double ts = current_ts;
                if (scan_count < static_cast<int>(index.size())) {
                    ts = index[scan_count].pc_ts_rel;
                }

                scans_.push_back(std::move(sd));
                timestamps_.push_back(ts);
                scan_count++;
            }
        } catch (...) {
            // Skip malformed packets
            continue;
        }
    }

    spdlog::info("PcapReader: Loaded {} scans from {}", scan_count, pcap_path);
    return !scans_.empty();
}

bool PcapReader::getPointCloud(int frame_idx, std::vector<float>& xyz_out, int& num_points) {
    if (frame_idx < 0 || frame_idx >= static_cast<int>(scans_.size())) {
        num_points = 0;
        return false;
    }

    auto& sd = scans_[frame_idx];
    xyz_out = sd.xyz;
    num_points = sd.num_points;
    return true;
}

void PcapReader::close() {
    scans_.clear();
    timestamps_.clear();
}

} // namespace msl
