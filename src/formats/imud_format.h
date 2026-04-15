#pragma once

#include "core/sensor_data.h"
#include <cstdint>
#include <vector>
#include <fstream>

namespace msl {

/// IMUD binary record: 40 bytes fixed-size.
/// Matches the Python struct format '=dQffffff'
#pragma pack(push, 1)
struct ImudRecord {
    double   pc_ts_rel;      // 0:  8B - relative timestamp
    uint64_t sensor_ts_ns;   // 8:  8B - sensor nanosecond timestamp
    float    accel_x;        // 16: 4B - m/s^2
    float    accel_y;        // 20: 4B
    float    accel_z;        // 24: 4B
    float    gyro_x;         // 28: 4B - rad/s
    float    gyro_y;         // 32: 4B
    float    gyro_z;         // 36: 4B
};
static_assert(sizeof(ImudRecord) == 40, "ImudRecord must be 40 bytes");
#pragma pack(pop)

/// IMUDI index record: 16 bytes.
#pragma pack(push, 1)
struct ImudiRecord {
    double  pc_ts_rel;  // 8B
    int64_t position;   // 8B - byte offset in IMUD file
};
static_assert(sizeof(ImudiRecord) == 16, "ImudiRecord must be 16 bytes");
#pragma pack(pop)

class ImudFormat {
public:
    /// Pack ImuData into a 40-byte record.
    static ImudRecord pack(const ImuData& imu) {
        ImudRecord rec{};
        rec.pc_ts_rel   = imu.pc_ts_rel;
        rec.sensor_ts_ns = imu.sensor_ts_ns;
        rec.accel_x     = imu.accel[0];
        rec.accel_y     = imu.accel[1];
        rec.accel_z     = imu.accel[2];
        rec.gyro_x      = imu.gyro[0];
        rec.gyro_y      = imu.gyro[1];
        rec.gyro_z      = imu.gyro[2];
        return rec;
    }

    /// Unpack a record into ImuData.
    static ImuData unpack(const ImudRecord& rec) {
        ImuData imu;
        imu.pc_ts_rel   = rec.pc_ts_rel;
        imu.sensor_ts_ns = rec.sensor_ts_ns;
        imu.accel[0]    = rec.accel_x;
        imu.accel[1]    = rec.accel_y;
        imu.accel[2]    = rec.accel_z;
        imu.gyro[0]     = rec.gyro_x;
        imu.gyro[1]     = rec.gyro_y;
        imu.gyro[2]     = rec.gyro_z;
        return imu;
    }

    /// Read all index records from an IMUDI file.
    static std::vector<ImudiRecord> readIndex(const std::string& path) {
        std::vector<ImudiRecord> records;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return records;

        ImudiRecord rec;
        while (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
            records.push_back(rec);
        }
        return records;
    }
};

} // namespace msl
