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

    // Multicast (for coexistence with ROS primary subscriber)
    bool        lidar_multicast_enabled = false;
    std::string lidar_multicast_dest    = "239.201.201.201";  // Multicast group
    std::string lidar_mtp_dest          = "";                 // NIC IP to bind (empty = auto)
    bool        lidar_mtp_main          = false;              // viewer = passive subscriber

    // --- Radar ---
    std::string radar_ip        = "192.168.172.128";
    int         radar_port      = 7;        // BSR20 TCP port
    int         radar_udp_port  = 9002;     // BSR30 UDP port
    std::string radar_sdk       = "BSR20";  // "BSR20" or "BSR30"

    // --- Camera ---
    /// Number of independent camera channels the logger supports.
    /// Channel 0 is stored in config.ini as [Camera], channel 1 as [Camera2].
    static constexpr int kNumCameras = 2;

    /// Per-channel camera settings. Each channel gets its own worker,
    /// logger (Camera/<session>_cam.mov, _cam2.mov) and preview view.
    struct CameraSettings {
        bool        enabled      = true;    // channel shown/usable in the UI
        std::string type         = "RTSP";  // "RTSP" or "Webcam"
        std::string rtsp_url     = "";
        int         webcam_index = 0;
        int         width        = 1920;
        int         height       = 1080;
        int         fps          = 30;
        /// 이 카메라 스트림의 고정 지연 보정값(ms). RTSP 카메라는 인코딩+네트워크
        /// +지터버퍼 때문에 라이다/레이더보다 늦게 도착한다. RTCP 로 카메라 시각을
        /// 알 수 없는 경우(NTP 미동기) 여기에 측정한 지연을 넣으면 기록 PTS 에서
        /// 그만큼 빼서 다른 센서와 시간축을 맞춘다.
        int         time_offset_ms = 0;
        /// 기록 타임스탬프의 기준.
        ///   "auto" : 카메라 시각(RTCP)이 신뢰 가능하면 스트림 지연을 자동 보정,
        ///            아니면 도착 시각(PC 시계) 사용 — 기본값
        ///   "pc"   : 카메라 시각을 아예 쓰지 않고 항상 도착 시각(PC 시계) 사용
        std::string time_source = "auto";
    };

    CameraSettings cameras[kNumCameras];

    AppConfig() {
        cameras[0].rtsp_url = "rtsp://admin:byda!1026@192.168.172.215/profile2/media.smp";
        cameras[1].enabled  = false;        // second channel off until configured
        cameras[1].rtsp_url = "";
    }

    // --- GPS ---
    std::string gps_port        = "";       // Empty = auto-detect
    int         gps_baudrate    = 9600;

    // --- Recording ---
    std::string data_dir        = "Data";

    // --- Player ---
    /// Last folder used by the "Browse..." button on the Player panel.
    /// Saved across sessions so the dialog re-opens at the same place.
    std::string last_session_browse_dir = "";

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
        f << "imu_port = " << imu_port << "\n";
        f << "multicast_enabled = " << (lidar_multicast_enabled ? "true" : "false") << "\n";
        f << "multicast_dest = " << lidar_multicast_dest << "\n";
        f << "mtp_dest = " << lidar_mtp_dest << "\n";
        f << "mtp_main = " << (lidar_mtp_main ? "true" : "false") << "\n\n";

        f << "[Radar]\n";
        f << "ip = " << radar_ip << "\n";
        f << "port = " << radar_port << "\n";
        f << "udp_port = " << radar_udp_port << "\n";
        f << "sdk = " << radar_sdk << "\n\n";

        for (int i = 0; i < kNumCameras; ++i) {
            const CameraSettings& c = cameras[i];
            f << (i == 0 ? "[Camera]\n" : "[Camera" + std::to_string(i + 1) + "]\n");
            f << "enabled = " << (c.enabled ? "true" : "false") << "\n";
            f << "type = " << c.type << "\n";
            f << "rtsp_url = " << c.rtsp_url << "\n";
            f << "webcam_index = " << c.webcam_index << "\n";
            f << "width = " << c.width << "\n";
            f << "height = " << c.height << "\n";
            f << "fps = " << c.fps << "\n";
            f << "time_offset_ms = " << c.time_offset_ms << "\n";
            f << "time_source = " << c.time_source << "\n\n";
        }

        f << "[GPS]\n";
        f << "port = " << gps_port << "\n";
        f << "baudrate = " << gps_baudrate << "\n\n";

        f << "[Recording]\n";
        f << "data_dir = " << data_dir << "\n\n";

        f << "[Player]\n";
        f << "last_session_browse_dir = " << last_session_browse_dir << "\n\n";

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
            if      (key == "ip")                 lidar_ip = val;
            else if (key == "lidar_port")         lidar_port = std::stoi(val);
            else if (key == "imu_port")           imu_port = std::stoi(val);
            else if (key == "multicast_enabled")  lidar_multicast_enabled = (val == "true" || val == "1");
            else if (key == "multicast_dest")     lidar_multicast_dest = val;
            else if (key == "mtp_dest")           lidar_mtp_dest = val;
            else if (key == "mtp_main")           lidar_mtp_main = (val == "true" || val == "1");
        }
        else if (section == "Radar") {
            if      (key == "ip")       radar_ip = val;
            else if (key == "port")     radar_port = std::stoi(val);
            else if (key == "udp_port") radar_udp_port = std::stoi(val);
            else if (key == "sdk")      radar_sdk = val;
        }
        else if (section.rfind("Camera", 0) == 0) {
            // [Camera] -> channel 0, [Camera2] -> channel 1, ...
            int idx = 0;
            if (section.size() > 6) {
                try { idx = std::stoi(section.substr(6)) - 1; }
                catch (...) { return; }
            }
            if (idx < 0 || idx >= kNumCameras) return;
            CameraSettings& c = cameras[idx];
            if      (key == "enabled")       c.enabled = (val == "true" || val == "1");
            else if (key == "type")          c.type = val;
            else if (key == "rtsp_url")      c.rtsp_url = val;
            else if (key == "webcam_index")  c.webcam_index = std::stoi(val);
            else if (key == "width")         c.width = std::stoi(val);
            else if (key == "height")        c.height = std::stoi(val);
            else if (key == "fps")           c.fps = std::stoi(val);
            else if (key == "time_offset_ms") c.time_offset_ms = std::stoi(val);
            else if (key == "time_source")    c.time_source = val;
        }
        else if (section == "GPS") {
            if      (key == "port")     gps_port = val;
            else if (key == "baudrate") gps_baudrate = std::stoi(val);
        }
        else if (section == "Recording") {
            if (key == "data_dir") data_dir = val;
        }
        else if (section == "Player") {
            if (key == "last_session_browse_dir") last_session_browse_dir = val;
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
