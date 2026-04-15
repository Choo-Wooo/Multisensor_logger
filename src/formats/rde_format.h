#pragma once

#include "core/sensor_data.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>

namespace msl {

// ==================== RDE Constants ====================

static constexpr uint8_t kScanPattern[8] = {0xFE, 0xCA, 0xEF, 0xBE, 0xAA, 0x55, 0xAA, 0x55};

// Track bit-packing scale factors
static constexpr float kTrackXScale   = 0.125f;   // meters
static constexpr float kTrackYScale   = 0.125f;
static constexpr float kTrackVxScale  = 0.5f;     // km/h
static constexpr float kTrackVyScale  = 0.125f;   // km/h

// Bit widths
static constexpr int kIdBits     = 8;
static constexpr int kXPosBits   = 13;
static constexpr int kYPosBits   = 13;
static constexpr int kXVelBits   = 10;
static constexpr int kYVelBits   = 12;
static constexpr int kTypeBits   = 3;
static constexpr int kStatusBits = 5;

// ==================== RDEI Index Record ====================

#pragma pack(push, 1)
struct RdeiRecord {
    double  timestamp;    // 8B - pc_ts_rel
    int64_t position;     // 8B - byte offset in RDE (to prefix)
    int32_t scan_index;   // 4B
    int32_t length;       // 4B - packet size (excluding prefix)
};
static_assert(sizeof(RdeiRecord) == 24, "RdeiRecord must be 24 bytes");
#pragma pack(pop)

// ==================== RDE Format ====================

class RdeFormat {
public:
    /// Pack a single radar track into 8 bytes (64-bit unsigned LE).
    static uint64_t packTrack(const RadarTrack& track) {
        uint64_t val = 0;
        int bit = 0;

        auto packField = [&](int raw, int bits) {
            uint64_t mask = (1ULL << bits) - 1;
            uint64_t u = static_cast<uint64_t>(raw) & mask;
            val |= (u << bit);
            bit += bits;
        };

        auto toRaw = [](float value, float scale, int bits) -> int {
            int raw = static_cast<int>(value / scale);
            if (raw < 0) raw += (1 << bits);
            return raw;
        };

        packField(track.id,                                       kIdBits);
        packField(toRaw(track.x_pos, kTrackXScale, kXPosBits),   kXPosBits);
        packField(toRaw(track.y_pos, kTrackYScale, kYPosBits),   kYPosBits);
        packField(toRaw(track.x_vel, kTrackVxScale, kXVelBits),  kXVelBits);
        packField(toRaw(track.y_vel, kTrackVyScale, kYVelBits),  kYVelBits);
        packField(track.type,                                     kTypeBits);
        packField(track.status,                                   kStatusBits);

        return val;
    }

    /// Unpack 8 bytes into a RadarTrack.
    static RadarTrack unpackTrack(uint64_t val) {
        RadarTrack t;
        int bit = 0;

        auto unpackField = [&](int bits) -> int {
            uint64_t mask = (1ULL << bits) - 1;
            int v = static_cast<int>((val >> bit) & mask);
            bit += bits;
            return v;
        };

        auto toFloat = [](int raw, float scale, int bits) -> float {
            if (raw >= (1 << (bits - 1))) raw -= (1 << bits);
            return raw * scale;
        };

        t.id     = unpackField(kIdBits);
        int xr   = unpackField(kXPosBits);
        int yr   = unpackField(kYPosBits);
        int vxr  = unpackField(kXVelBits);
        int vyr  = unpackField(kYVelBits);
        t.type   = unpackField(kTypeBits);
        t.status = unpackField(kStatusBits);

        t.x_pos = toFloat(xr, kTrackXScale, kXPosBits);
        t.y_pos = toFloat(yr, kTrackYScale, kYPosBits);
        t.x_vel = toFloat(vxr, kTrackVxScale, kXVelBits);
        t.y_vel = toFloat(vyr, kTrackVyScale, kYVelBits);

        return t;
    }

    /// Build a complete RDE packet (prefix + scan_pattern + header + params + tracks).
    /// Returns the full binary packet ready to write to file.
    static std::vector<uint8_t> buildPacket(uint32_t frame_index,
                                             const RadarScanData& scan) {
        int num_tracks = static_cast<int>(scan.tracks.size());
        int object_count = 5 + num_tracks;  // 5 empty params + N tracks

        // Calculate sizes
        int header_size = 16;
        int params_size = 5 * 8;  // 5 empty 8-byte parameters
        int tracks_size = num_tracks * 8;
        int scan_packet_size = 8 + header_size + params_size + tracks_size;  // pattern + header + objects
        int total_size = 4 + scan_packet_size;  // prefix + scan_packet

        std::vector<uint8_t> buf(total_size, 0);
        uint8_t* p = buf.data();

        // Prefix (4 bytes LE)
        std::memcpy(p, &frame_index, 4);
        p += 4;

        // Scan pattern (8 bytes)
        std::memcpy(p, kScanPattern, 8);
        p += 8;

        // Header (16 bytes)
        uint16_t hdr_len = 16;
        uint16_t obj_cnt = static_cast<uint16_t>(object_count);
        uint32_t scan_idx = scan.scan_index;
        uint32_t ts = scan.timestamp;
        uint16_t err = scan.error_flag;

        std::memcpy(p + 0, &hdr_len, 2);
        std::memcpy(p + 2, &obj_cnt, 2);
        std::memcpy(p + 4, &scan_idx, 4);
        std::memcpy(p + 8, &ts, 4);
        std::memcpy(p + 12, &err, 2);

        // Compute checksums
        uint8_t obj_checksum = 0;
        // Object data starts after header
        // For now, compute after filling objects

        p += 16;

        // Empty parameters (5 x 8 bytes of zeros - already zeroed)
        p += params_size;

        // Tracks
        for (const auto& track : scan.tracks) {
            uint64_t packed = packTrack(track);
            std::memcpy(p, &packed, 8);
            p += 8;
        }

        // Compute object checksum (XOR of all object data bytes)
        uint8_t* obj_start = buf.data() + 4 + 8 + 16;  // after prefix + pattern + header
        int obj_data_size = params_size + tracks_size;
        obj_checksum = 0;
        for (int i = 0; i < obj_data_size; ++i) {
            obj_checksum ^= obj_start[i];
        }
        buf[4 + 8 + 14] = obj_checksum;

        // Header checksum (XOR of first 15 bytes of header including obj_checksum)
        uint8_t hdr_checksum = 0;
        uint8_t* hdr_start = buf.data() + 4 + 8;
        for (int i = 0; i < 15; ++i) {
            hdr_checksum ^= hdr_start[i];
        }
        buf[4 + 8 + 15] = hdr_checksum;

        return buf;
    }

    /// Write RDEI index file.
    static bool writeIndex(const std::string& path, const std::vector<RdeiRecord>& records) {
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        uint32_t count = static_cast<uint32_t>(records.size());
        f.write(reinterpret_cast<const char*>(&count), 4);
        for (const auto& rec : records) {
            f.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
        }
        return true;
    }

    /// Read RDEI index file.
    static std::vector<RdeiRecord> readIndex(const std::string& path) {
        std::vector<RdeiRecord> records;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return records;

        uint32_t count = 0;
        f.read(reinterpret_cast<char*>(&count), 4);
        records.resize(count);
        f.read(reinterpret_cast<char*>(records.data()), count * sizeof(RdeiRecord));
        return records;
    }
};

} // namespace msl
