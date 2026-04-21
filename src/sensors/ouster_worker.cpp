#include "ouster_worker.h"
#include <spdlog/spdlog.h>
#include <memory>

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

    try {
        osdk_core::SensorConfig config;
        config.udp_port_lidar = lidar_port_;
        config.udp_port_imu = imu_port_;

        // Only difference between unicast and multicast is the udp_dest value.
        // SensorPacketSource handles both cases.
        if (multicast_enabled_) {
            spdlog::info("Ouster: Multicast connecting to {} (group:{})",
                         ip_, multicast_dest_);
            config.udp_dest = multicast_dest_;
        } else {
            spdlog::info("Ouster: Unicast connecting to {} (lidar:{}, imu:{})",
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

            spdlog::info("Ouster: Connected. {}x{}, fw:{}",
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

    while (running_ && !disconnect_requested_) {
        try {
            auto event = impl_->source->get_packet(1.0);

            if (event.type == osdk_sensor::ClientEvent::EventType::POLL_TIMEOUT) continue;
            if (event.type == osdk_sensor::ClientEvent::EventType::ERR ||
                event.type == osdk_sensor::ClientEvent::EventType::EXIT) break;
            if (event.type != osdk_sensor::ClientEvent::EventType::PACKET) continue;

            auto& pkt = event.packet();

            // --- IMU Packet ---
            if (pkt.type() == osdk_core::PacketType::Imu) {
                auto& imu_pkt = static_cast<osdk_core::ImuPacket&>(pkt);

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

                // Save every raw packet for pcap recording
                bool scan_complete = (*impl_->batcher)(lidar_pkt, *impl_->scan);

                if (recording_ && on_raw_lidar_packet) {
                    on_raw_lidar_packet(lidar_pkt.buf, ts_rel, scan_complete);
                }

                if (scan_complete) {
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

        } catch (const std::exception& e) {
            spdlog::error("Ouster: Poll error: {}", e.what());
            break;
        }
    }
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
    spdlog::info("Ouster: Disconnected");
}

} // namespace msl
