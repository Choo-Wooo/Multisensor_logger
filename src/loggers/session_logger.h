#pragma once

#include "core/app_config.h"
#include "core/session_info.h"
#include "core/clock.h"
#include <string>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

namespace msl {

/// Creates session directory structure and writes session_info.ini.
class SessionLogger {
public:
    /// Create the full session directory structure.
    /// Returns the session directory path.
    static std::string createSession(const std::string& base_dir,
                                      const std::string& session_name,
                                      const AppConfig& config,
                                      double start_time) {
        std::string session_dir = base_dir + "/" + session_name;

        // Create directories
        MKDIR(base_dir.c_str());
        MKDIR(session_dir.c_str());
        MKDIR((session_dir + "/Lidar").c_str());
        MKDIR((session_dir + "/Radar").c_str());
        MKDIR((session_dir + "/Camera").c_str());
        MKDIR((session_dir + "/GPS").c_str());
        MKDIR((session_dir + "/IMU").c_str());

        // Write session_info.ini
        SessionConfig cfg;
        cfg.session_name = session_name;
        cfg.recording_start_time_utc = Clock::toUtcString(start_time);
        cfg.recording_start_time_unix = start_time;
        cfg.lidar_ip = config.lidar_ip;
        cfg.lidar_port = config.lidar_port;
        cfg.imu_port = config.imu_port;
        cfg.radar_ip = config.radar_ip;
        cfg.radar_port = config.radar_port;
        cfg.radar_sdk = config.radar_sdk;
        cfg.camera_type = config.camera_type;
        cfg.camera_width = config.camera_width;
        cfg.camera_height = config.camera_height;
        cfg.camera_rtsp_url = config.camera_rtsp_url;
        cfg.gps_port = config.gps_port;
        cfg.gps_baudrate = config.gps_baudrate;

        SessionInfo::write(session_dir + "/session_info.ini", cfg);

        spdlog::info("Session created: {}", session_dir);
        return session_dir;
    }

    /// Generate file paths for each sensor within a session.
    struct FilePaths {
        std::string pcap;        // Lidar/session.pcap
        std::string pcap_idx;    // Lidar/session.pcap.idx
        std::string lidar_meta;  // Lidar/session_meta.json
        std::string rde;        // Radar/session.rde
        std::string rdei;       // Radar/session.rdei
        std::string radar_ini;  // Radar/session.ini
        std::string mp4;        // Camera/session_cam.mp4
        std::string gpsd;       // GPS/session.gpsd
        std::string gpsdi;      // GPS/session.gpsdi
        std::string imud;       // IMU/session.imud
        std::string imudi;      // IMU/session.imudi
    };

    static FilePaths getFilePaths(const std::string& session_dir,
                                   const std::string& session_name) {
        FilePaths paths;
        paths.pcap       = session_dir + "/Lidar/" + session_name + ".pcap";
        paths.pcap_idx   = session_dir + "/Lidar/" + session_name + ".pcap.idx";
        paths.lidar_meta = session_dir + "/Lidar/" + session_name + "_meta.json";
        paths.rde       = session_dir + "/Radar/" + session_name + ".rde";
        paths.rdei      = session_dir + "/Radar/" + session_name + ".rdei";
        paths.radar_ini = session_dir + "/Radar/" + session_name + ".ini";
        paths.mp4       = session_dir + "/Camera/" + session_name + "_cam.mp4";
        paths.gpsd      = session_dir + "/GPS/" + session_name + ".gpsd";
        paths.gpsdi     = session_dir + "/GPS/" + session_name + ".gpsdi";
        paths.imud      = session_dir + "/IMU/" + session_name + ".imud";
        paths.imudi     = session_dir + "/IMU/" + session_name + ".imudi";
        return paths;
    }
};

} // namespace msl
