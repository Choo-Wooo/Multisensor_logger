#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace msl {

/// Simple INI-based configuration manager replacing Python config.py.
/// Reads/writes config.ini with section.key = value pairs.
class AppConfig {
public:
    // --- Lidar ---
    std::string lidar_ip        = "192.168.172.129";
    int         lidar_port      = 7502;
    int         imu_port        = 7503;

    // --- Radar ---
    std::string radar_ip        = "192.168.172.128";
    int         radar_port      = 7;        // BSR20 TCP port
    int         radar_udp_port  = 9002;     // BSR30 UDP port
    std::string radar_sdk       = "BSR20";  // "BSR20" or "BSR30"

    // --- Camera ---
    std::string camera_type     = "RTSP";   // "RTSP" or "Webcam"
    std::string camera_rtsp_url = "rtsp://admin:byda!1026@192.168.172.215/profile2/media.smp";
    int         camera_webcam_index = 0;
    int         camera_width    = 1920;
    int         camera_height   = 1080;
    int         camera_fps      = 30;

    // --- GPS ---
    std::string gps_port        = "";       // Empty = auto-detect
    int         gps_baudrate    = 9600;

    // --- Recording ---
    std::string data_dir        = "Data";

    // --- UI ---
    int         render_interval_ms = 33;    // ~30fps

    // --- Map ---
    double      origin_lat      = 37.555503632236;
    double      origin_lon      = 126.973751343837;

    /// Load from INI file. Missing keys keep defaults.
    bool load(const std::string& path) {
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

            applyValue(section, key, val);
        }
        return true;
    }

    /// Save current config to INI file.
    bool save(const std::string& path) const {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "[Lidar]\n";
        f << "ip = " << lidar_ip << "\n";
        f << "lidar_port = " << lidar_port << "\n";
        f << "imu_port = " << imu_port << "\n\n";

        f << "[Radar]\n";
        f << "ip = " << radar_ip << "\n";
        f << "port = " << radar_port << "\n";
        f << "udp_port = " << radar_udp_port << "\n";
        f << "sdk = " << radar_sdk << "\n\n";

        f << "[Camera]\n";
        f << "type = " << camera_type << "\n";
        f << "rtsp_url = " << camera_rtsp_url << "\n";
        f << "webcam_index = " << camera_webcam_index << "\n";
        f << "width = " << camera_width << "\n";
        f << "height = " << camera_height << "\n";
        f << "fps = " << camera_fps << "\n\n";

        f << "[GPS]\n";
        f << "port = " << gps_port << "\n";
        f << "baudrate = " << gps_baudrate << "\n\n";

        f << "[Recording]\n";
        f << "data_dir = " << data_dir << "\n\n";

        f << "[UI]\n";
        f << "render_interval_ms = " << render_interval_ms << "\n\n";

        f << "[Map]\n";
        f << "origin_lat = " << origin_lat << "\n";
        f << "origin_lon = " << origin_lon << "\n";

        return true;
    }

private:
    void applyValue(const std::string& section, const std::string& key, const std::string& val) {
        if (section == "Lidar") {
            if      (key == "ip")         lidar_ip = val;
            else if (key == "lidar_port") lidar_port = std::stoi(val);
            else if (key == "imu_port")   imu_port = std::stoi(val);
        }
        else if (section == "Radar") {
            if      (key == "ip")       radar_ip = val;
            else if (key == "port")     radar_port = std::stoi(val);
            else if (key == "udp_port") radar_udp_port = std::stoi(val);
            else if (key == "sdk")      radar_sdk = val;
        }
        else if (section == "Camera") {
            if      (key == "type")          camera_type = val;
            else if (key == "rtsp_url")      camera_rtsp_url = val;
            else if (key == "webcam_index")  camera_webcam_index = std::stoi(val);
            else if (key == "width")         camera_width = std::stoi(val);
            else if (key == "height")        camera_height = std::stoi(val);
            else if (key == "fps")           camera_fps = std::stoi(val);
        }
        else if (section == "GPS") {
            if      (key == "port")     gps_port = val;
            else if (key == "baudrate") gps_baudrate = std::stoi(val);
        }
        else if (section == "Recording") {
            if (key == "data_dir") data_dir = val;
        }
        else if (section == "UI") {
            if (key == "render_interval_ms") render_interval_ms = std::stoi(val);
        }
        else if (section == "Map") {
            if      (key == "origin_lat") origin_lat = std::stod(val);
            else if (key == "origin_lon") origin_lon = std::stod(val);
        }
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
};

} // namespace msl
