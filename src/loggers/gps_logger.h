#pragma once

#include "base_logger.h"
#include "core/sensor_data.h"
#include "formats/gpsd_format.h"
#include <fstream>
#include <string>

namespace msl {

/// GPS logger: writes GPSD (52B records) + GPSDI (16B index) binary files.
class GpsLogger : public BaseLogger<std::pair<double, GpsFix>> {
public:
    GpsLogger(const std::string& gpsd_path, const std::string& gpsdi_path)
        : gpsd_path_(gpsd_path), gpsdi_path_(gpsdi_path) {}

protected:
    const char* loggerName() const override { return "GpsLogger"; }

    void onStart() override {
        gpsd_file_.open(gpsd_path_, std::ios::binary);
        gpsdi_file_.open(gpsdi_path_, std::ios::binary);
        file_position_ = 0;
    }

    void writeItem(const std::pair<double, GpsFix>& item) override {
        double pc_ts_rel = item.first;
        const GpsFix& fix = item.second;

        // Write GPSD record (52 bytes)
        GpsdRecord rec = GpsdFormat::pack(pc_ts_rel, fix);
        gpsd_file_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));

        // Write GPSDI index record (16 bytes)
        GpsdiRecord idx;
        idx.pc_ts_rel = pc_ts_rel;
        idx.position  = file_position_;
        gpsdi_file_.write(reinterpret_cast<const char*>(&idx), sizeof(idx));

        file_position_ += sizeof(GpsdRecord);
    }

    void onStop() override {
        gpsd_file_.close();
        gpsdi_file_.close();
    }

private:
    std::string gpsd_path_;
    std::string gpsdi_path_;
    std::ofstream gpsd_file_;
    std::ofstream gpsdi_file_;
    int64_t file_position_ = 0;
};

} // namespace msl
