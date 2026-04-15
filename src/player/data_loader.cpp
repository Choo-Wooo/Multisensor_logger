#include "data_loader.h"
#include "loggers/lidar_logger.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace msl {

bool DataLoader::loadSession(const std::string& session_dir, SessionData& data) {
    data.session_dir = session_dir;

    // Read session_info.ini
    std::string ini_path = session_dir + "/session_info.ini";
    if (!SessionInfo::read(ini_path, data.config)) {
        spdlog::error("DataLoader: Failed to read {}", ini_path);
        return false;
    }

    std::string name = data.config.session_name;

    // Load each sensor (non-fatal if missing)
    data.has_radar  = loadRadar(session_dir, name, data);
    data.has_gps    = loadGps(session_dir, name, data);
    data.has_imu    = loadImu(session_dir, name, data);
    data.has_camera = loadCamera(session_dir, name, data);
    data.has_lidar  = loadLidar(session_dir, name, data);

    // Compute total duration from all sensors
    double max_ts = 0;
    auto checkMax = [&](const std::vector<double>& ts) {
        if (!ts.empty()) max_ts = std::max(max_ts, ts.back());
    };
    checkMax(data.lidar_timestamps);
    checkMax(data.radar_timestamps);
    checkMax(data.camera_timestamps);
    checkMax(data.gps_timestamps);
    checkMax(data.imu_timestamps);
    data.total_duration = max_ts;

    spdlog::info("DataLoader: Session '{}' loaded. Duration: {:.1f}s", name, data.total_duration);
    return true;
}

bool DataLoader::loadRadar(const std::string& session_dir, const std::string& name, SessionData& data) {
    std::string rdei_path = session_dir + "/Radar/" + name + ".rdei";
    std::string rde_path  = session_dir + "/Radar/" + name + ".rde";

    if (!fs::exists(rdei_path) || !fs::exists(rde_path)) return false;

    // Load RDEI index
    data.radar_index = RdeFormat::readIndex(rdei_path);
    if (data.radar_index.empty()) return false;

    // Extract timestamps
    data.radar_timestamps.reserve(data.radar_index.size());
    for (const auto& rec : data.radar_index) {
        data.radar_timestamps.push_back(rec.timestamp);
    }

    // Load entire RDE file into memory for fast access
    std::ifstream rde_file(rde_path, std::ios::binary | std::ios::ate);
    auto size = rde_file.tellg();
    rde_file.seekg(0);
    data.rde_data.resize(static_cast<size_t>(size));
    rde_file.read(reinterpret_cast<char*>(data.rde_data.data()), size);

    spdlog::info("DataLoader: Radar loaded ({} frames)", data.radar_index.size());
    return true;
}

bool DataLoader::loadGps(const std::string& session_dir, const std::string& name, SessionData& data) {
    std::string gpsd_path  = session_dir + "/GPS/" + name + ".gpsd";
    std::string gpsdi_path = session_dir + "/GPS/" + name + ".gpsdi";

    if (!fs::exists(gpsd_path)) return false;

    // Load all GPSD records
    std::ifstream f(gpsd_path, std::ios::binary);
    GpsdRecord rec;
    while (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
        data.gps_records.push_back(rec);
        data.gps_timestamps.push_back(rec.pc_ts_rel);
    }

    spdlog::info("DataLoader: GPS loaded ({} records)", data.gps_records.size());
    return !data.gps_records.empty();
}

bool DataLoader::loadImu(const std::string& session_dir, const std::string& name, SessionData& data) {
    std::string imud_path  = session_dir + "/IMU/" + name + ".imud";

    if (!fs::exists(imud_path)) return false;

    std::ifstream f(imud_path, std::ios::binary);
    ImudRecord rec;
    while (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
        data.imu_records.push_back(rec);
        data.imu_timestamps.push_back(rec.pc_ts_rel);
    }

    spdlog::info("DataLoader: IMU loaded ({} records)", data.imu_records.size());
    return !data.imu_records.empty();
}

bool DataLoader::loadCamera(const std::string& session_dir, const std::string& name, SessionData& data) {
    data.mp4_path = session_dir + "/Camera/" + name + "_cam.mp4";
    if (!fs::exists(data.mp4_path)) return false;

    // TODO: Use FFmpeg API to read MP4 metadata (frame count, PTS array)
    // For now, mark as available
    spdlog::info("DataLoader: Camera MP4 found");
    return true;
}

bool DataLoader::loadLidar(const std::string& session_dir, const std::string& name, SessionData& data) {
    data.pcap_path      = session_dir + "/Lidar/" + name + ".pcap";
    data.pcap_idx_path  = session_dir + "/Lidar/" + name + ".pcap.idx";
    data.lidar_meta_path = session_dir + "/Lidar/" + name + "_meta.json";

    if (!fs::exists(data.pcap_path)) return false;

    // Load pcap index if available
    if (fs::exists(data.pcap_idx_path)) {
        std::ifstream f(data.pcap_idx_path, std::ios::binary);
        uint32_t count = 0;
        f.read(reinterpret_cast<char*>(&count), 4);

        PcapIndexRecord rec;
        for (uint32_t i = 0; i < count; ++i) {
            if (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
                data.lidar_timestamps.push_back(rec.pc_ts_rel);
            }
        }
    }

    spdlog::info("DataLoader: Lidar pcap found ({} indexed frames)", data.lidar_timestamps.size());
    return true;
}

} // namespace msl
