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

/// Camera frame from CameraWorker (프리뷰용 RGB24).
/// RTSP 경로에서는 화면 표시용으로 축소된 크기가 들어온다 (width/height).
/// 원본 해상도는 src_width/src_height 로 따로 전달한다.
struct CameraFrame {
    /// I420(YUV420 planar) 데이터: Y(w*h) + U(w/2*h/2) + V(w/2*h/2) 를 연속 저장.
    /// 디코더 출력 포맷 그대로라 CPU 색공간 변환이 필요 없다.
    /// 화면 표시는 GPU 셰이더가, 녹화(재인코딩 폴백)는 인코더가 바로 받는다.
    std::vector<uint8_t> yuv_data;
    /// RGB24 (USB 카메라 등 YUV 를 주지 않는 소스 / 레거시 경로)
    std::vector<uint8_t> rgb_data;
    int    width = 0;
    int    height = 0;
    int    src_width = 0;           // 디코딩된 원본 해상도 (0 = width 와 동일)
    int    src_height = 0;
    bool   yuv_full_range = false;  // yuvj420p (0~255) 인지 여부
    double pc_ts_rel = 0.0;

    int srcWidth()  const { return src_width  > 0 ? src_width  : width;  }
    int srcHeight() const { return src_height > 0 ? src_height : height; }
    bool hasYuv() const { return !yuv_data.empty(); }
    bool hasRgb() const { return !rgb_data.empty(); }
    /// I420 평면 시작 위치
    const uint8_t* planeY() const { return yuv_data.data(); }
    const uint8_t* planeU() const { return yuv_data.data() + (size_t)width * height; }
    const uint8_t* planeV() const {
        return yuv_data.data() + (size_t)width * height + (size_t)(width / 2) * (height / 2);
    }
};

/// 압축 상태 그대로(H.264 AnnexB 액세스 유닛) 저장하기 위한 패킷.
/// 카메라가 보내온 비트스트림을 재인코딩 없이 MOV 로 muxing 할 때 사용한다.
struct CameraNalPacket {
    std::vector<uint8_t> data;      // AnnexB (00 00 00 01 시작코드 포함)
    double pc_ts_rel = 0.0;
    bool   keyframe = false;
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
