#include "ouster_worker.h"
#include "core/logging.h"
#include "core/clock.h"
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <memory>

#include <ouster/client.h>
#include <ouster/sensor_packet_source.h>
#include <ouster/types.h>
#include <ouster/xyzlut.h>
#include <ouster/lidar_scan.h>
#include <ouster/packet.h>

namespace msl {

namespace osdk_sensor = ouster::sdk::sensor;
namespace osdk_core   = ouster::sdk::core;

struct OusterWorker::Impl {
    std::unique_ptr<osdk_sensor::SensorPacketSource> source;
    std::vector<std::shared_ptr<osdk_core::SensorInfo>> infos;
    std::unique_ptr<osdk_core::ScanBatcher> batcher;
    osdk_core::XYZLut xyz_lut;
    std::unique_ptr<osdk_core::LidarScan> scan;
    bool lut_ready = false;
};

bool OusterWorker::onConnect() {
    impl_ = new Impl();

    // Route the SDK's own diagnostics into logs/lidar/ouster_sdk.log.
    // ClientEvent only carries ERR/EXIT with no message, so the real cause
    // (socket error, metadata failure, buffer overflow) is only visible here.
    static bool sdk_logger_ready = false;
    if (!sdk_logger_ready) {
        sdk_logger_ready = true;
        const std::string& base = msl::log::baseDir();
        if (!base.empty()) {
            std::string sdk_log =
                (std::filesystem::path(base) / "lidar" / "ouster_sdk.log").string();
            bool ok = osdk_sensor::init_logger("info", sdk_log, /*rotating=*/true,
                                               64 * 1024 * 1024, 4);
            msl::log::lidar()->info("Ouster: SDK log -> {} ({})", sdk_log,
                                    ok ? "ok" : "failed, SDK logs stay on console");
        }
    }

    try {
        osdk_core::SensorConfig config;
        config.udp_port_lidar = lidar_port_;
        config.udp_port_imu = imu_port_;

        // Only difference between unicast and multicast is the udp_dest value.
        // SensorPacketSource handles both cases.
        if (multicast_enabled_) {
            msl::log::lidar()->info("Ouster: Multicast connecting to {} (group:{})",
                         ip_, multicast_dest_);
            config.udp_dest = multicast_dest_;
        } else {
            msl::log::lidar()->info("Ouster: Unicast connecting to {} (lidar:{}, imu:{})",
                         ip_, lidar_port_, imu_port_);
            config.udp_dest = "@auto";
        }

        osdk_sensor::Sensor sensor(ip_, config);
        std::vector<osdk_sensor::Sensor> sensors = {sensor};

        // reuse_ports=true allows multiple subscribers on the same port
        // (essential when ROS and this viewer coexist on the same PC in multicast mode).
        impl_->source = std::make_unique<osdk_sensor::SensorPacketSource>(
            sensors,
            std::vector<osdk_core::SensorInfo>{},  // empty: let SDK fetch metadata
            45.0,                                   // config_timeout_sec
            0.0,                                    // buffer_time_sec
            true                                    // reuse_ports
        );
        impl_->infos = impl_->source->sensor_info();

        if (!impl_->infos.empty()) {
            auto& info = *impl_->infos[0];
            impl_->xyz_lut = osdk_core::make_xyz_lut(info, true);
            impl_->batcher = std::make_unique<osdk_core::ScanBatcher>(info);
            impl_->scan = std::make_unique<osdk_core::LidarScan>(info);
            impl_->lut_ready = true;

            msl::log::lidar()->info("Ouster: Connected. {}x{}, fw:{}",
                         info.format.columns_per_frame,
                         info.format.pixels_per_column,
                         info.fw_rev);
        }
    } catch (const std::exception& e) {
        notifyError(std::string("Ouster: Connection failed: ") + e.what());
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    return true;
}

void OusterWorker::pollLoop() {
    if (!impl_ || !impl_->source) return;

    // --- Stall/stop diagnostics -------------------------------------------
    // The loop used to exit silently on ERR/EXIT, so a stop left no trace of
    // its cause. Every exit path now logs why, with the counters needed to
    // tell "network went away" from "batcher never completed a scan".
    constexpr double kStallWarnSec  = 3.0;   // no packet at all
    constexpr double kScanWarnSec   = 3.0;   // packets arrive but no full scan
    constexpr double kHeartbeatSec  = 5.0;   // periodic debug counters

    const double loop_start = Clock::steady();
    double last_packet_ts   = loop_start;
    double last_scan_ts     = loop_start;
    double last_heartbeat   = loop_start;

    uint64_t lidar_packets = 0, imu_packets = 0, scans = 0;
    uint64_t hb_packets = 0, hb_scans = 0;
    bool packet_stall_warned = false;
    bool scan_stall_warned   = false;
    std::string exit_reason  = "stop requested";

    auto secondsSince = [](double t) { return Clock::steady() - t; };

    auto heartbeat = [&](double now) {
        if (now - last_heartbeat < kHeartbeatSec) return;
        double dt = now - last_heartbeat;
        msl::log::lidar()->debug(
            "Ouster: alive — {:.1f} pkt/s, {:.2f} scan/s (total pkt={}, imu={}, scans={}), "
            "sdk_dropped={}, since_last_pkt={:.2f}s, since_last_scan={:.2f}s",
            (lidar_packets - hb_packets) / dt,
            (scans - hb_scans) / dt,
            lidar_packets, imu_packets, scans,
            impl_->source->dropped_packets(),
            now - last_packet_ts, now - last_scan_ts);
        last_heartbeat = now;
        hb_packets = lidar_packets;
        hb_scans = scans;
    };

    while (running_ && !disconnect_requested_) {
        try {
            auto event = impl_->source->get_packet(1.0);
            double now = Clock::steady();

            if (event.type == osdk_sensor::ClientEvent::EventType::POLL_TIMEOUT) {
                // No UDP data for 1 s. The sensor, the switch or the multicast
                // route is the usual cause — the SDK itself is still healthy.
                if (!packet_stall_warned && now - last_packet_ts > kStallWarnSec) {
                    msl::log::lidar()->warn(
                        "Ouster: no lidar/imu packet for {:.1f}s (pkt={}, scans={}, "
                        "sdk_dropped={}) — sensor, network or multicast route down?",
                        now - last_packet_ts, lidar_packets, scans,
                        impl_->source->dropped_packets());
                    packet_stall_warned = true;
                }
                heartbeat(now);
                continue;
            }

            if (event.type == osdk_sensor::ClientEvent::EventType::ERR ||
                event.type == osdk_sensor::ClientEvent::EventType::EXIT) {
                const bool is_err =
                    (event.type == osdk_sensor::ClientEvent::EventType::ERR);
                exit_reason = is_err ? "SDK reported ERR" : "SDK reported EXIT";
                msl::log::lidar()->error(
                    "Ouster: {} (source={}) after {:.1f}s — pkt={}, imu={}, scans={}, "
                    "sdk_dropped={}, since_last_pkt={:.2f}s, since_last_scan={:.2f}s. "
                    "Streaming stops here; see ouster_sdk.log for the SDK-side cause.",
                    exit_reason, event.source, now - loop_start,
                    lidar_packets, imu_packets, scans,
                    impl_->source->dropped_packets(),
                    now - last_packet_ts, now - last_scan_ts);
                break;
            }

            if (event.type != osdk_sensor::ClientEvent::EventType::PACKET) {
                heartbeat(now);
                continue;
            }

            if (packet_stall_warned) {
                msl::log::lidar()->info("Ouster: packets resumed after {:.1f}s gap",
                                        now - last_packet_ts);
                packet_stall_warned = false;
            }
            last_packet_ts = now;

            auto& pkt = event.packet();

            // --- IMU Packet ---
            if (pkt.type() == osdk_core::PacketType::Imu) {
                auto& imu_pkt = static_cast<osdk_core::ImuPacket&>(pkt);
                imu_packets++;

                ImuData imu;
                if (recording_) {
                    imu.pc_ts_rel = getRelativeTime();
                }
                imu.sensor_ts_ns = imu_pkt.gyro_ts();

                auto acc = imu_pkt.accel();   // Eigen::Array<float, N, 3>
                auto gyr = imu_pkt.gyro();

                int last = static_cast<int>(acc.rows()) - 1;
                if (last >= 0) {
                    imu.accel[0] = acc(last, 0);
                    imu.accel[1] = acc(last, 1);
                    imu.accel[2] = acc(last, 2);
                    imu.gyro[0]  = gyr(last, 0);
                    imu.gyro[1]  = gyr(last, 1);
                    imu.gyro[2]  = gyr(last, 2);
                }

                if (on_imu_data_ready) {
                    auto cb = on_imu_data_ready;
                    event_bus_.post([cb, imu]() { cb(imu); });
                }
            }

            // --- Lidar Packet ---
            if (pkt.type() == osdk_core::PacketType::Lidar && impl_->batcher && impl_->scan) {
                auto& lidar_pkt = static_cast<osdk_core::LidarPacket&>(pkt);
                double ts_rel = recording_ ? getRelativeTime() : 0.0;
                lidar_packets++;

                // Save every raw packet for pcap recording
                bool scan_complete = (*impl_->batcher)(lidar_pkt, *impl_->scan);

                if (recording_ && on_raw_lidar_packet) {
                    on_raw_lidar_packet(lidar_pkt.buf, ts_rel, scan_complete);
                }

                // Packets keep coming but no frame is completed: a batcher /
                // column-window problem, not a network problem. Without this
                // the UI simply freezes with no message at all.
                if (!scan_complete && !scan_stall_warned &&
                    now - last_scan_ts > kScanWarnSec) {
                    msl::log::lidar()->warn(
                        "Ouster: packets arriving but no complete scan for {:.1f}s "
                        "(pkt={}, scans={}) — frame assembly stalled",
                        now - last_scan_ts, lidar_packets, scans);
                    scan_stall_warned = true;
                }

                if (scan_complete) {
                    if (scan_stall_warned) {
                        msl::log::lidar()->info("Ouster: scan assembly recovered after {:.1f}s",
                                                now - last_scan_ts);
                        scan_stall_warned = false;
                    }
                    last_scan_ts = now;
                    scans++;

                    // Full scan ready — compute XYZ
                    LidarScanData data;

                    if (impl_->lut_ready) {
                        auto cloud = osdk_core::cartesian(*impl_->scan, impl_->xyz_lut);
                        int num_points = static_cast<int>(cloud.rows());

                        data.num_points = num_points;
                        data.rows = static_cast<int>(impl_->scan->h);
                        data.cols = static_cast<int>(impl_->scan->w);
                        data.xyz.resize(num_points * 3);

                        for (int i = 0; i < num_points; ++i) {
                            data.xyz[i * 3 + 0] = static_cast<float>(cloud(i, 0));
                            data.xyz[i * 3 + 1] = static_cast<float>(cloud(i, 1));
                            data.xyz[i * 3 + 2] = static_cast<float>(cloud(i, 2));
                        }
                    }

                    data.pc_ts_rel = ts_rel;
                    frame_count_++;

                    if (on_lidar_scan_ready) {
                        on_lidar_scan_ready(data);
                    }
                }
            }

            heartbeat(now);

        } catch (const std::exception& e) {
            exit_reason = std::string("exception: ") + e.what();
            msl::log::lidar()->error(
                "Ouster: poll exception after {:.1f}s — {} (pkt={}, imu={}, scans={}, "
                "sdk_dropped={}, since_last_pkt={:.2f}s)",
                secondsSince(loop_start), e.what(), lidar_packets, imu_packets, scans,
                impl_->source->dropped_packets(), secondsSince(last_packet_ts));
            break;
        }
    }

    if (!running_ || disconnect_requested_) {
        exit_reason = "stop requested";
    }

    msl::log::lidar()->warn(
        "Ouster: poll loop exited — reason='{}', ran {:.1f}s, pkt={}, imu={}, scans={}, "
        "sdk_dropped={}. No data will arrive until Lidar is toggled off/on.",
        exit_reason, secondsSince(loop_start), lidar_packets, imu_packets, scans,
        impl_->source ? impl_->source->dropped_packets() : 0);
}

std::string OusterWorker::getMetadataJson() const {
    if (impl_ && !impl_->infos.empty()) {
        return impl_->infos[0]->to_json_string();
    }
    return "";
}

void OusterWorker::onDisconnect() {
    if (impl_) {
        impl_->source.reset();
        impl_->batcher.reset();
        impl_->scan.reset();
        delete impl_;
        impl_ = nullptr;
    }
    msl::log::lidar()->info("Ouster: Disconnected (frames={})", frame_count_.load());
}

} // namespace msl
