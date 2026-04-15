#include "lidar_logger.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace msl {

void LidarLogger::start() {
    if (running_) return;

    pcap_file_.open(pcap_path_, std::ios::binary);
    file_offset_ = 0;
    index_records_.clear();
    writePcapGlobalHeader();

    running_ = true;
    thread_ = std::thread(&LidarLogger::writeLoop, this);
}

void LidarLogger::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();

    pcap_file_.close();

    // Write index
    std::ofstream idx_file(idx_path_, std::ios::binary);
    uint32_t count = static_cast<uint32_t>(index_records_.size());
    idx_file.write(reinterpret_cast<const char*>(&count), 4);
    for (const auto& rec : index_records_) {
        idx_file.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    }

    spdlog::info("LidarLogger: {} packets, {} scans indexed",
                 file_offset_, index_records_.size());
}

void LidarLogger::writeLoop() {
    while (running_ || !queue_.empty()) {
        LidarRawPacket pkt;
        if (!queue_.try_pop(pkt, std::chrono::milliseconds(100))) continue;

        // Record scan start offset for index (before writing first packet of scan)
        if (pkt.is_scan_complete) {
            PcapIndexRecord idx;
            idx.pc_ts_rel = pkt.pc_ts_rel;
            idx.file_offset = file_offset_;
            index_records_.push_back(idx);
        }

        // Write raw packet to pcap
        writePcapPacket(pkt.data.data(), pkt.data.size(), pkt.pc_ts_rel);
    }
}

void LidarLogger::writePcapGlobalHeader() {
    uint32_t magic     = 0xA1B2C3D4;
    uint16_t ver_major = 2;
    uint16_t ver_minor = 4;
    int32_t  thiszone  = 0;
    uint32_t sigfigs   = 0;
    uint32_t snaplen   = 65535;
    uint32_t network   = 1;

    pcap_file_.write(reinterpret_cast<const char*>(&magic), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&ver_major), 2);
    pcap_file_.write(reinterpret_cast<const char*>(&ver_minor), 2);
    pcap_file_.write(reinterpret_cast<const char*>(&thiszone), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&sigfigs), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&snaplen), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&network), 4);
    file_offset_ = 24;
}

void LidarLogger::writePcapPacket(const uint8_t* data, size_t size, double timestamp) {
    // Build Ethernet + IP + UDP header for Ouster Studio compatibility
    // Uses actual sensor IP and port
    uint8_t eth_ip_udp_hdr[42] = {
        // Ethernet header (14 bytes)
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst MAC (broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src MAC
        0x08, 0x00,                            // EtherType: IPv4
        // IP header (20 bytes)
        0x45, 0x00,                            // Version+IHL, DSCP
        0x00, 0x00,                            // Total length (filled below)
        0x00, 0x00, 0x00, 0x00,                // ID, Flags+Offset
        0x40, 0x11,                            // TTL=64, Protocol=UDP
        0x00, 0x00,                            // Header checksum (0 = skip)
        0x00, 0x00, 0x00, 0x00,                // Src IP (filled below)
        0xff, 0xff, 0xff, 0xff,                // Dst IP (broadcast)
        // UDP header (8 bytes)
        0x00, 0x00,                            // Src port (filled below)
        0x00, 0x00,                            // Dst port (filled below)
        0x00, 0x00,                            // UDP length (filled below)
        0x00, 0x00,                            // UDP checksum (0 = skip)
    };

    // Fill sensor IP
    unsigned int a, b, c, d;
    if (sscanf(sensor_ip_.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        eth_ip_udp_hdr[26] = static_cast<uint8_t>(a);
        eth_ip_udp_hdr[27] = static_cast<uint8_t>(b);
        eth_ip_udp_hdr[28] = static_cast<uint8_t>(c);
        eth_ip_udp_hdr[29] = static_cast<uint8_t>(d);
    }

    // Fill ports
    uint16_t port_be = htons(static_cast<uint16_t>(lidar_port_));
    std::memcpy(eth_ip_udp_hdr + 34, &port_be, 2);
    std::memcpy(eth_ip_udp_hdr + 36, &port_be, 2);

    uint32_t total_len = static_cast<uint32_t>(42 + size);
    uint8_t hdr[42];
    std::memcpy(hdr, eth_ip_udp_hdr, 42);

    // Fill IP total length (20 + 8 + payload)
    uint16_t ip_len = htons(static_cast<uint16_t>(20 + 8 + size));
    std::memcpy(hdr + 16, &ip_len, 2);

    // Fill UDP length (8 + payload)
    uint16_t udp_len = htons(static_cast<uint16_t>(8 + size));
    std::memcpy(hdr + 38, &udp_len, 2);

    // pcap record header
    uint32_t ts_sec  = static_cast<uint32_t>(timestamp);
    uint32_t ts_usec = static_cast<uint32_t>((timestamp - ts_sec) * 1000000);
    uint32_t incl_len = total_len;
    uint32_t orig_len = total_len;

    pcap_file_.write(reinterpret_cast<const char*>(&ts_sec), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&ts_usec), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&incl_len), 4);
    pcap_file_.write(reinterpret_cast<const char*>(&orig_len), 4);
    pcap_file_.write(reinterpret_cast<const char*>(hdr), 42);
    pcap_file_.write(reinterpret_cast<const char*>(data), size);

    file_offset_ += 16 + static_cast<int64_t>(total_len);
}

} // namespace msl
