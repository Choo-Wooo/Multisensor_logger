#pragma once

#include "core/session_info.h"
#include "core/sensor_data.h"
#include "formats/rde_format.h"
#include "formats/gpsd_format.h"
#include "formats/imud_format.h"
#include <string>
#include <vector>
#include <map>

namespace msl {

/// Loaded session data for playback.
struct SessionData {
    SessionConfig config;
    std::string session_dir;

    // Per-sensor availability
    bool has_lidar  = false;
    bool has_radar  = false;
    bool has_camera = false;
    bool has_gps    = false;
    bool has_imu    = false;

    // Timestamps arrays (relative, sorted)
    std::vector<double> lidar_timestamps;
    std::vector<double> radar_timestamps;
    std::vector<double> camera_timestamps;
    std::vector<double> gps_timestamps;
    std::vector<double> imu_timestamps;

    // Radar index records (for lazy-loading RDE scans)
    std::vector<RdeiRecord> radar_index;
    std::vector<uint8_t>    rde_data;  // Full RDE file in memory

    // GPS/IMU records (all loaded into memory)
    std::vector<GpsdRecord> gps_records;
    std::vector<ImudRecord> imu_records;

    // Camera metadata
    int    camera_frame_count = 0;
    double camera_fps = 30.0;

    // Lidar pcap path (for PcapReader)
    std::string pcap_path;
    std::string pcap_idx_path;
    std::string lidar_meta_path;

    // Camera MP4 path
    std::string mp4_path;

    // Total duration
    double total_duration = 0.0;

    /// Get list of available sensor names.
    std::vector<std::string> availableSensors() const {
        std::vector<std::string> sensors;
        if (has_lidar)  sensors.push_back("Lidar");
        if (has_radar)  sensors.push_back("Radar");
        if (has_camera) sensors.push_back("Camera");
        if (has_gps)    sensors.push_back("GPS");
        if (has_imu)    sensors.push_back("IMU");
        return sensors;
    }
};

/// Loads a recorded session for playback.
class DataLoader {
public:
    /// Load session from directory path.
    /// Returns true on success, populates data.
    static bool loadSession(const std::string& session_dir, SessionData& data);

private:
    static bool loadRadar(const std::string& session_dir, const std::string& name, SessionData& data);
    static bool loadGps(const std::string& session_dir, const std::string& name, SessionData& data);
    static bool loadImu(const std::string& session_dir, const std::string& name, SessionData& data);
    static bool loadCamera(const std::string& session_dir, const std::string& name, SessionData& data);
    static bool loadLidar(const std::string& session_dir, const std::string& name, SessionData& data);
};

} // namespace msl
