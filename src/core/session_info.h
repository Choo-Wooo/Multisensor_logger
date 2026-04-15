#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>

namespace msl {

/// Reads and writes session_info.ini files.
/// Preserves the exact format used by the Python logger for compatibility.
struct SessionConfig {
    // [SESSION]
    std::string session_name;
    std::string recording_start_time_utc;   // ISO 8601
    double      recording_start_time_unix = 0.0;

    // [LIDAR]
    std::string lidar_ip;
    int         lidar_port = 0;
    int         imu_port = 0;

    // [RADAR]
    std::string radar_ip;
    int         radar_port = 0;
    std::string radar_sdk;

    // [CAMERA]
    std::string camera_type;
    int         camera_width = 0;
    int         camera_height = 0;
    std::string camera_rtsp_url;

    // [GPS]
    std::string gps_port;
    int         gps_baudrate = 0;
};

class SessionInfo {
public:
    /// Write session_info.ini to the given path.
    static bool write(const std::string& path, const SessionConfig& cfg) {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "[SESSION]\n";
        f << "session_name = " << cfg.session_name << "\n";
        f << "recording_start_time_utc = " << cfg.recording_start_time_utc << "\n";
        f << std::fixed;
        f.precision(6);
        f << "recording_start_time_unix = " << cfg.recording_start_time_unix << "\n";
        f << "\n";

        f << "[LIDAR]\n";
        f << "ip = " << cfg.lidar_ip << "\n";
        f << "lidar_port = " << cfg.lidar_port << "\n";
        f << "imu_port = " << cfg.imu_port << "\n";
        f << "\n";

        f << "[RADAR]\n";
        f << "ip = " << cfg.radar_ip << "\n";
        f << "port = " << cfg.radar_port << "\n";
        f << "sdk = " << cfg.radar_sdk << "\n";
        f << "\n";

        f << "[CAMERA]\n";
        f << "type = " << cfg.camera_type << "\n";
        f << "width = " << cfg.camera_width << "\n";
        f << "height = " << cfg.camera_height << "\n";
        f << "rtsp_url = " << cfg.camera_rtsp_url << "\n";
        f << "\n";

        f << "[GPS]\n";
        f << "port = " << cfg.gps_port << "\n";
        f << "baudrate = " << cfg.gps_baudrate << "\n";

        return true;
    }

    /// Read session_info.ini from the given path.
    static bool read(const std::string& path, SessionConfig& cfg) {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        std::string line, section;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line[0] == '[') {
                auto end = line.find(']');
                if (end != std::string::npos)
                    section = line.substr(1, end - 1);
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));

            if (section == "SESSION") {
                if      (key == "session_name")               cfg.session_name = val;
                else if (key == "recording_start_time_utc")   cfg.recording_start_time_utc = val;
                else if (key == "recording_start_time_unix")  cfg.recording_start_time_unix = std::stod(val);
            }
            else if (section == "LIDAR") {
                if      (key == "ip")         cfg.lidar_ip = val;
                else if (key == "lidar_port") cfg.lidar_port = std::stoi(val);
                else if (key == "imu_port")   cfg.imu_port = std::stoi(val);
            }
            else if (section == "RADAR") {
                if      (key == "ip")   cfg.radar_ip = val;
                else if (key == "port") cfg.radar_port = std::stoi(val);
                else if (key == "sdk")  cfg.radar_sdk = val;
            }
            else if (section == "CAMERA") {
                if      (key == "type")      cfg.camera_type = val;
                else if (key == "width")     cfg.camera_width = std::stoi(val);
                else if (key == "height")    cfg.camera_height = std::stoi(val);
                else if (key == "rtsp_url")  cfg.camera_rtsp_url = val;
            }
            else if (section == "GPS") {
                if      (key == "port")     cfg.gps_port = val;
                else if (key == "baudrate") cfg.gps_baudrate = std::stoi(val);
            }
        }
        return true;
    }

private:
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
};

} // namespace msl
