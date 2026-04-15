#pragma once

#include <string>
#include <fstream>

namespace msl {

/// Radar configuration INI (saved alongside RDE files).
struct RadarConfig {
    int   eth_ver = 2;
    float inst_height = 8.0f;
    float inst_distance = 0.0f;
    float inst_angle_pitch = 0.0f;
    float inst_angle_azimuth = 0.0f;
    int   inst_direction = 1;
    float inst_speed_offset = 0.0f;
    float detect_line_1 = 30.0f;
};

class RadarIni {
public:
    static bool write(const std::string& path, const RadarConfig& cfg) {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "[BASIC]\n";
        f << "Radar_Eth_Ver = " << cfg.eth_ver << "\n\n";

        f << "[INSTALL]\n";
        f << "inst_height = " << cfg.inst_height << "\n";
        f << "inst_distance = " << cfg.inst_distance << "\n";
        f << "inst_angle_Pitch = " << cfg.inst_angle_pitch << "\n";
        f << "inst_angle_Azimuth = " << cfg.inst_angle_azimuth << "\n";
        f << "inst_direction = " << cfg.inst_direction << "\n";
        f << "inst_speedOffset = " << cfg.inst_speed_offset << "\n\n";

        f << "[DETECTION]\n";
        f << "detect_detectionLine_1 = " << cfg.detect_line_1 << "\n";

        return true;
    }
};

} // namespace msl
