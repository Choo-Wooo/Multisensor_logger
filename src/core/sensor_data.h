#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace msl {

// ==================== Common Sensor Data Types ====================

/// Lidar scan data from OusterWorker
struct LidarScanData {
    std::vector<float> xyz;         // Flattened XYZ points (N*3)
    int num_points = 0;
    int rows = 0;
    int cols = 0;
    double pc_ts_rel = 0.0;
    std::vector<uint8_t> raw_packets; // Raw UDP packets for pcap recording
};

/// IMU data from OusterWorker
struct ImuData {
    double   pc_ts_rel = 0.0;
    uint64_t sensor_ts_ns = 0;
    float    accel[3] = {0, 0, 0};
    float    gyro[3]  = {0, 0, 0};
};

/// Single radar track
struct RadarTrack {
    int     id = 0;
    float   x_pos = 0.0f;      // meters
    float   y_pos = 0.0f;      // meters
    float   x_vel = 0.0f;      // m/s or km/h depending on SDK
    float   y_vel = 0.0f;
    int     type = 0;
    int     status = 0;
};

/// Radar scan data from RadarWorker
struct RadarScanData {
    std::vector<RadarTrack> tracks;
    uint32_t scan_index = 0;
    uint32_t timestamp = 0;     // Radar internal timestamp
    uint16_t error_flag = 0;
    double   pc_ts_rel = 0.0;
};

/// Camera frame from CameraWorker
struct CameraFrame {
    std::vector<uint8_t> rgb_data;  // RGB24 pixel data
    int    width = 0;
    int    height = 0;
    double pc_ts_rel = 0.0;
};

/// GPS fix data from GpsWorker
struct GpsFix {
    double   latitude = 0.0;        // Decimal degrees
    double   longitude = 0.0;
    float    altitude = 0.0f;       // Meters MSL
    float    speed_kmh = 0.0f;
    float    heading = 0.0f;        // Degrees
    float    hdop = 99.9f;
    float    vdop = 99.9f;
    float    pdop = 99.9f;
    uint8_t  fix_quality = 0;       // 0=none, 1=GPS, 2=DGPS, 4=RTK
    uint8_t  satellites = 0;
    bool     valid = false;
    double   timestamp = 0.0;       // GPS UTC timestamp
    double   pc_ts_rel = 0.0;
};

} // namespace msl
