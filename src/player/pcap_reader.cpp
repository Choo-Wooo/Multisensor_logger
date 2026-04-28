#include "pcap_reader.h"
#include "loggers/lidar_logger.h"  // PcapIndexRecord (READ-ONLY use; logger is unchanged)
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <mutex>

// Ouster SDK
#include <ouster/types.h>
#include <ouster/xyzlut.h>
#include <ouster/lidar_scan.h>
#include <ouster/packet.h>

namespace fs = std::filesystem;
namespace osdk_core = ouster::sdk::core;

namespace msl {

namespace {

// Pcap-record header layout (libpcap classic format)
struct PcapRecordHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

constexpr int kPcapGlobalHeaderBytes = 24;

// Strip Ethernet(14) + IP(20) + UDP(8) = 42 byte header if present.
inline void stripEthIpUdp(const std::vector<uint8_t>& buf,
                          const uint8_t*& payload, size_t& payload_size) {
    payload = buf.data();
    payload_size = buf.size();
    if (buf.size() > 42 && buf[12] == 0x08 && buf[13] == 0x00) {
        payload += 42;
        payload_size -= 42;
    }
}

} // namespace

// ===================================================================
// PIMPL
// ===================================================================
struct PcapReader::Impl {
    // Per-scan: byte offset in pcap file where the FIRST packet of the scan starts
    std::vector<int64_t> scan_offsets;
    std::vector<double> timestamps;

    // Persistent state for on-demand decoding
    std::ifstream pcap_file;
    std::shared_ptr<osdk_core::SensorInfo> info;
    std::shared_ptr<osdk_core::PacketFormat> packet_format;
    std::shared_ptr<osdk_core::XYZLut> xyz_lut;

    // Cache last decoded scan (for sequential access speedup)
    int cached_idx = -1;
    std::vector<float> cached_xyz;
    int cached_num_points = 0;

    // Protects pcap_file + decode (getPointCloud may be called from playback/UI threads)
    std::mutex mtx;

    bool decodeScanAt(int64_t start_offset, std::vector<float>& xyz, int& num_points) {
        // Caller holds mtx
        if (!pcap_file.is_open() || !info || !packet_format || !xyz_lut) {
            num_points = 0;
            return false;
        }

        pcap_file.clear();
        pcap_file.seekg(start_offset);
        if (!pcap_file.good()) { num_points = 0; return false; }

        osdk_core::ScanBatcher batcher(*info);
        osdk_core::LidarScan scan(*info);

        bool got_scan = false;

        while (pcap_file.good() && !pcap_file.eof()) {
            PcapRecordHeader hdr;
            pcap_file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
            if (!pcap_file.good()) break;

            if (hdr.incl_len == 0 || hdr.incl_len > 100000) break;

            std::vector<uint8_t> buf(hdr.incl_len);
            pcap_file.read(reinterpret_cast<char*>(buf.data()), hdr.incl_len);
            if (!pcap_file.good() && !pcap_file.eof()) break;

            try {
                const uint8_t* payload;
                size_t payload_size;
                stripEthIpUdp(buf, payload, payload_size);

                if (payload_size != packet_format->lidar_packet_size) continue;

                osdk_core::LidarPacket pkt(static_cast<int>(packet_format->lidar_packet_size));
                pkt.format = packet_format;
                std::memcpy(pkt.buf.data(), payload, packet_format->lidar_packet_size);

                if (batcher(pkt, scan)) {
                    got_scan = true;
                    break;
                }
            } catch (...) {
                continue;
            }
        }

        if (!got_scan) { num_points = 0; return false; }

        auto cloud = osdk_core::cartesian(scan, *xyz_lut);
        int n = static_cast<int>(cloud.rows());
        num_points = n;
        xyz.resize(static_cast<size_t>(n) * 3);
        for (int i = 0; i < n; ++i) {
            xyz[i * 3 + 0] = static_cast<float>(cloud(i, 0));
            xyz[i * 3 + 1] = static_cast<float>(cloud(i, 1));
            xyz[i * 3 + 2] = static_cast<float>(cloud(i, 2));
        }
        return true;
    }
};

// ===================================================================
// PcapReader (public methods forward to Impl)
// ===================================================================
PcapReader::PcapReader() : impl_(std::make_unique<Impl>()) {}
PcapReader::~PcapReader() { close(); }

int PcapReader::frameCount() const {
    return static_cast<int>(impl_->scan_offsets.size());
}

const std::vector<double>& PcapReader::timestamps() const {
    return impl_->timestamps;
}

bool PcapReader::open(const std::string& pcap_path, const std::string& metadata_path) {
    close();
    impl_ = std::make_unique<Impl>();

    try {
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

        impl_->info = std::make_shared<osdk_core::SensorInfo>(meta_json);
        impl_->packet_format = std::make_shared<osdk_core::PacketFormat>(*impl_->info);
        impl_->xyz_lut = std::make_shared<osdk_core::XYZLut>(
            osdk_core::make_xyz_lut(*impl_->info, true));

        // Open pcap file (kept open for lifetime)
        impl_->pcap_file.open(pcap_path, std::ios::binary);
        if (!impl_->pcap_file.is_open()) {
            spdlog::error("PcapReader: Failed to open {}", pcap_path);
            return false;
        }

        // Skip pcap global header
        impl_->pcap_file.seekg(kPcapGlobalHeaderBytes);

        // Optional sidecar index file (file format unchanged from logger side)
        const std::string idx_path = pcap_path + ".idx";
        std::vector<PcapIndexRecord> sidecar_index;
        if (fs::exists(idx_path)) {
            std::ifstream idx_file(idx_path, std::ios::binary);
            uint32_t count = 0;
            idx_file.read(reinterpret_cast<char*>(&count), 4);
            sidecar_index.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                idx_file.read(reinterpret_cast<char*>(&sidecar_index[i]),
                              sizeof(PcapIndexRecord));
            }
        }

        // Walk the pcap once, recording the FILE OFFSET where each scan starts.
        // We use ScanBatcher to detect scan boundaries; we do NOT compute or
        // store XYZ here — that's done lazily in getPointCloud().
        osdk_core::ScanBatcher batcher(*impl_->info);
        osdk_core::LidarScan scan(*impl_->info);

        int64_t scan_start_offset = impl_->pcap_file.tellg();
        bool wrote_first_offset = false;

        int scan_count = 0;
        double current_ts = 0.0;

        while (impl_->pcap_file.good() && !impl_->pcap_file.eof()) {
            int64_t pkt_start_off = impl_->pcap_file.tellg();
            if (pkt_start_off < 0) break;

            PcapRecordHeader hdr;
            impl_->pcap_file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
            if (!impl_->pcap_file.good()) break;

            if (hdr.incl_len == 0 || hdr.incl_len > 100000) break;

            std::vector<uint8_t> buf(hdr.incl_len);
            impl_->pcap_file.read(reinterpret_cast<char*>(buf.data()), hdr.incl_len);
            if (!impl_->pcap_file.good() && !impl_->pcap_file.eof()) break;

            current_ts = hdr.ts_sec + hdr.ts_usec / 1000000.0;

            try {
                const uint8_t* payload;
                size_t payload_size;
                stripEthIpUdp(buf, payload, payload_size);

                if (payload_size != impl_->packet_format->lidar_packet_size) continue;

                if (!wrote_first_offset) {
                    scan_start_offset = pkt_start_off;
                    wrote_first_offset = true;
                }

                osdk_core::LidarPacket pkt(
                    static_cast<int>(impl_->packet_format->lidar_packet_size));
                pkt.format = impl_->packet_format;
                std::memcpy(pkt.buf.data(), payload, impl_->packet_format->lidar_packet_size);
                pkt.host_timestamp = static_cast<uint64_t>(current_ts * 1e9);

                if (batcher(pkt, scan)) {
                    double ts = current_ts;
                    if (scan_count < static_cast<int>(sidecar_index.size())) {
                        ts = sidecar_index[scan_count].pc_ts_rel;
                    }

                    impl_->scan_offsets.push_back(scan_start_offset);
                    impl_->timestamps.push_back(ts);
                    scan_count++;

                    // Next scan starts at next packet
                    scan_start_offset = impl_->pcap_file.tellg();
                    // Reset scan buffer for next iteration
                    scan = osdk_core::LidarScan(*impl_->info);
                }
            } catch (const std::exception& e) {
                spdlog::warn("PcapReader: Skipping malformed packet: {}", e.what());
                continue;
            } catch (...) {
                continue;
            }
        }

        // Clear EOF/fail bits so subsequent seekg+read works
        impl_->pcap_file.clear();

        spdlog::info("PcapReader: Indexed {} scans (lazy-load mode, ~{} KB metadata)",
                      scan_count,
                      (impl_->scan_offsets.size() + impl_->timestamps.size()) * 8 / 1024);

        return !impl_->scan_offsets.empty();
    } catch (const std::exception& e) {
        spdlog::error("PcapReader: Failed to load {}: {}", pcap_path, e.what());
        close();
        return false;
    } catch (...) {
        spdlog::error("PcapReader: Failed to load {}", pcap_path);
        close();
        return false;
    }
}

bool PcapReader::getPointCloud(int frame_idx, std::vector<float>& xyz_out, int& num_points) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (frame_idx < 0 || frame_idx >= static_cast<int>(impl_->scan_offsets.size())) {
        num_points = 0;
        return false;
    }

    // Cache hit (sequential access pattern)
    if (frame_idx == impl_->cached_idx && !impl_->cached_xyz.empty()) {
        xyz_out = impl_->cached_xyz;
        num_points = impl_->cached_num_points;
        return true;
    }

    int64_t off = impl_->scan_offsets[frame_idx];
    bool ok = impl_->decodeScanAt(off, xyz_out, num_points);
    if (ok) {
        impl_->cached_idx = frame_idx;
        impl_->cached_xyz = xyz_out;
        impl_->cached_num_points = num_points;
    }
    return ok;
}

void PcapReader::close() {
    if (impl_) {
        if (impl_->pcap_file.is_open()) impl_->pcap_file.close();
        impl_->scan_offsets.clear();
        impl_->timestamps.clear();
        impl_->cached_idx = -1;
        impl_->cached_xyz.clear();
        impl_->cached_num_points = 0;
        impl_->info.reset();
        impl_->packet_format.reset();
        impl_->xyz_lut.reset();
    }
}

} // namespace msl
