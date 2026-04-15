#pragma once

#include "base_logger.h"
#include "core/sensor_data.h"
#include "formats/rde_format.h"
#include "formats/radar_ini.h"
#include <fstream>
#include <string>
#include <vector>

namespace msl {

/// Radar logger: writes RDE (binary tracks) + RDEI (index) + INI config files.
class RadarLogger : public BaseLogger<RadarScanData> {
public:
    RadarLogger(const std::string& rde_path, const std::string& rdei_path,
                const std::string& ini_path)
        : rde_path_(rde_path), rdei_path_(rdei_path), ini_path_(ini_path) {}

    void setConfig(const RadarConfig& cfg) { config_ = cfg; }

protected:
    void onStart() override {
        rde_file_.open(rde_path_, std::ios::binary);
        file_position_ = 0;
        frame_index_ = 0;
        index_records_.clear();

        // Write radar INI config
        RadarIni::write(ini_path_, config_);
    }

    void writeItem(const RadarScanData& scan) override {
        // Build and write RDE packet
        auto packet = RdeFormat::buildPacket(frame_index_, scan);
        rde_file_.write(reinterpret_cast<const char*>(packet.data()), packet.size());

        // Record index entry
        RdeiRecord idx;
        idx.timestamp  = scan.pc_ts_rel;
        idx.position   = file_position_;
        idx.scan_index = static_cast<int32_t>(scan.scan_index);
        idx.length     = static_cast<int32_t>(packet.size() - 4);  // Exclude prefix
        index_records_.push_back(idx);

        file_position_ += static_cast<int64_t>(packet.size());
        frame_index_++;
    }

    void onStop() override {
        rde_file_.close();

        // Write RDEI index file
        RdeFormat::writeIndex(rdei_path_, index_records_);
    }

private:
    std::string rde_path_;
    std::string rdei_path_;
    std::string ini_path_;
    RadarConfig config_;

    std::ofstream rde_file_;
    int64_t file_position_ = 0;
    uint32_t frame_index_ = 0;
    std::vector<RdeiRecord> index_records_;
};

} // namespace msl
