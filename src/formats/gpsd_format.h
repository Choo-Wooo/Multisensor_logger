#pragma once

#include "core/sensor_data.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>

namespace msl {

/// GPSD binary record: 52 bytes fixed-size.
/// Matches the Python struct format '=ddddffffBBBx'
#pragma pack(push, 1)
struct GpsdRecord {
    double   pc_ts_rel;      // 0:  8B - relative timestamp
    double   gps_ts_utc;     // 8:  8B - GPS UTC timestamp
    double   latitude;       // 16: 8B - decimal degrees
    double   longitude;      // 24: 8B - decimal degrees
    float    altitude;       // 32: 4B - meters MSL
    float    speed_kmh;      // 36: 4B - km/h
    float    heading;        // 40: 4B - degrees
    float    hdop;           // 44: 4B - horizontal DOP
    uint8_t  fix_quality;    // 48: 1B
    uint8_t  satellites;     // 49: 1B
    uint8_t  valid;          // 50: 1B
    uint8_t  pad;            // 51: 1B - alignment padding
};
static_assert(sizeof(GpsdRecord) == 52, "GpsdRecord must be 52 bytes");
#pragma pack(pop)

/// GPSDI index record: 16 bytes.
#pragma pack(push, 1)
struct GpsdiRecord {
    double  pc_ts_rel;  // 8B
    int64_t position;   // 8B - byte offset in GPSD file
};
static_assert(sizeof(GpsdiRecord) == 16, "GpsdiRecord must be 16 bytes");
#pragma pack(pop)

class GpsdFormat {
public:
    /// Pack a GpsFix into a 52-byte GPSD record.
    static GpsdRecord pack(double pc_ts_rel, const GpsFix& fix) {
        GpsdRecord rec{};
        rec.pc_ts_rel   = pc_ts_rel;
        rec.gps_ts_utc  = fix.timestamp;
        rec.latitude    = fix.latitude;
        rec.longitude   = fix.longitude;
        rec.altitude    = fix.altitude;
        rec.speed_kmh   = fix.speed_kmh;
        rec.heading     = fix.heading;
        rec.hdop        = fix.hdop;
        rec.fix_quality = fix.fix_quality;
        rec.satellites  = fix.satellites;
        rec.valid       = fix.valid ? 1 : 0;
        rec.pad         = 0;
        return rec;
    }

    /// Unpack a 52-byte buffer into a GpsFix.
    static GpsFix unpack(const GpsdRecord& rec) {
        GpsFix fix;
        fix.latitude    = rec.latitude;
        fix.longitude   = rec.longitude;
        fix.altitude    = rec.altitude;
        fix.speed_kmh   = rec.speed_kmh;
        fix.heading     = rec.heading;
        fix.hdop        = rec.hdop;
        fix.fix_quality = rec.fix_quality;
        fix.satellites  = rec.satellites;
        fix.valid       = (rec.valid != 0);
        fix.timestamp   = rec.gps_ts_utc;
        fix.pc_ts_rel   = rec.pc_ts_rel;
        return fix;
    }

    /// Read all index records from a GPSDI file.
    static std::vector<GpsdiRecord> readIndex(const std::string& path) {
        std::vector<GpsdiRecord> records;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return records;

        GpsdiRecord rec;
        while (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
            records.push_back(rec);
        }
        return records;
    }
};

} // namespace msl
