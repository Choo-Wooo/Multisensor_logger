#include "lidar_logger.h"
#include "core/clock.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace msl {

namespace {

struct LidarPerfWindow {
    double report_start = 0.0;
    uint64_t packets = 0;
    uint64_t scans = 0;
    double total_queue_wait_ms = 0.0;
    double max_queue_wait_ms = 0.0;
    double total_write_ms = 0.0;
    double max_write_ms = 0.0;
    double total_packet_ms = 0.0;
    double max_packet_ms = 0.0;

    void reset(double now) {
        report_start = now;
        packets = 0;
        scans = 0;
        total_queue_wait_ms = 0.0;
        max_queue_wait_ms = 0.0;
        total_write_ms = 0.0;
        max_write_ms = 0.0;
        total_packet_ms = 0.0;
        max_packet_ms = 0.0;
    }
};

void updateMaxValue(std::atomic<size_t>& target, size_t candidate) {
    size_t current = target.load(std::memory_order_relaxed);
    while (candidate > current &&
           !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {
    }
}

} // namespace

void LidarLogger::enqueue(LidarRawPacket pkt) {
    QueuedLidarPacket queued;
    queued.packet = std::move(pkt);
    queued.enqueue_steady = Clock::steady();

    bool dropped = queue_.try_push(std::move(queued));
    size_t depth = queue_.size();
    updateMaxValue(max_queue_depth_, depth);

    if (dropped) {
        uint64_t drops = dropped_packets_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (drops == 1 || drops % 100 == 0) {
            spdlog::warn("LidarLogger queue overflow: dropped oldest queued packet (drops={}, depth={})",
                         drops, depth);
        }
    }
}

void LidarLogger::start() {
    if (started_.exchange(true, std::memory_order_relaxed)) return;

    pcap_file_.open(pcap_path_, std::ios::binary);
    file_offset_ = 0;
    index_records_.clear();
    dropped_packets_ = 0;
    max_queue_depth_ = 0;
    writePcapGlobalHeader();

    running_ = true;
    thread_ = std::thread(&LidarLogger::writeLoop, this);
}

void LidarLogger::stop() {
    if (!started_.exchange(false, std::memory_order_relaxed)) return;

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

    spdlog::debug("LidarLogger: stopped (bytes_written={}, scans_indexed={}, max_queue={}, queue_drops={})",
                  file_offset_,
                  index_records_.size(),
                  max_queue_depth_.load(std::memory_order_relaxed),
                  dropped_packets_.load(std::memory_order_relaxed));
}

void LidarLogger::writeLoop() {
    LidarPerfWindow perf;
    perf.reset(Clock::steady());

    while (running_ || !queue_.empty()) {
        QueuedLidarPacket queued;
        if (!queue_.try_pop(queued, std::chrono::milliseconds(100))) continue;

        double packet_begin = Clock::steady();
        double queue_wait_ms = (packet_begin - queued.enqueue_steady) * 1000.0;
        const LidarRawPacket& pkt = queued.packet;

        // Record scan start offset for index (before writing first packet of scan)
        if (pkt.is_scan_complete) {
            PcapIndexRecord idx;
            idx.pc_ts_rel = pkt.pc_ts_rel;
            idx.file_offset = file_offset_;
            index_records_.push_back(idx);
            perf.scans++;
        }

        double write_begin = Clock::steady();
        writePcapPacket(pkt.data.data(), pkt.data.size(), pkt.pc_ts_rel);
        double write_ms = (Clock::steady() - write_begin) * 1000.0;
        double packet_ms = (Clock::steady() - packet_begin) * 1000.0;

        perf.packets++;
        perf.total_queue_wait_ms += queue_wait_ms;
        perf.total_write_ms += write_ms;
        perf.total_packet_ms += packet_ms;
        if (queue_wait_ms > perf.max_queue_wait_ms) perf.max_queue_wait_ms = queue_wait_ms;
        if (write_ms > perf.max_write_ms) perf.max_write_ms = write_ms;
        if (packet_ms > perf.max_packet_ms) perf.max_packet_ms = packet_ms;

        double now = Clock::steady();
        if (now - perf.report_start >= 2.0) {
            spdlog::debug(
                "LidarLogger perf: packets={}, scans={}, queue={}, max_queue={}, avg_wait_ms={:.3f}, max_wait_ms={:.3f}, avg_write_ms={:.3f}, max_write_ms={:.3f}, avg_packet_ms={:.3f}, max_packet_ms={:.3f}, queue_drops={}",
                perf.packets,
                perf.scans,
                queue_.size(),
                max_queue_depth_.load(std::memory_order_relaxed),
                perf.total_queue_wait_ms / static_cast<double>(perf.packets),
                perf.max_queue_wait_ms,
                perf.total_write_ms / static_cast<double>(perf.packets),
                perf.max_write_ms,
                perf.total_packet_ms / static_cast<double>(perf.packets),
                perf.max_packet_ms,
                dropped_packets_.load(std::memory_order_relaxed));
            perf.reset(now);
        }
    }

    if (perf.packets > 0) {
        spdlog::debug(
            "LidarLogger perf: packets={}, scans={}, queue={}, max_queue={}, avg_wait_ms={:.3f}, max_wait_ms={:.3f}, avg_write_ms={:.3f}, max_write_ms={:.3f}, avg_packet_ms={:.3f}, max_packet_ms={:.3f}, queue_drops={}",
            perf.packets,
            perf.scans,
            queue_.size(),
            max_queue_depth_.load(std::memory_order_relaxed),
            perf.total_queue_wait_ms / static_cast<double>(perf.packets),
            perf.max_queue_wait_ms,
            perf.total_write_ms / static_cast<double>(perf.packets),
            perf.max_write_ms,
            perf.total_packet_ms / static_cast<double>(perf.packets),
            perf.max_packet_ms,
            dropped_packets_.load(std::memory_order_relaxed));
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
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00,
        // IP header (20 bytes)
        0x45, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff,
        // UDP header (8 bytes)
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
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
