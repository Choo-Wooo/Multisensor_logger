#pragma once

#include "base_logger.h"
#include "core/sensor_data.h"
#include "formats/imud_format.h"
#include <fstream>
#include <string>

namespace msl {

/// IMU logger: writes IMUD (40B records) + IMUDI (16B index) binary files.
class ImuLogger : public BaseLogger<ImuData> {
public:
    ImuLogger(const std::string& imud_path, const std::string& imudi_path)
        : imud_path_(imud_path), imudi_path_(imudi_path) {}

protected:
    const char* loggerName() const override { return "ImuLogger"; }

    void onStart() override {
        imud_file_.open(imud_path_, std::ios::binary);
        imudi_file_.open(imudi_path_, std::ios::binary);
        file_position_ = 0;
    }

    void writeItem(const ImuData& imu) override {
        // Write IMUD record (40 bytes)
        ImudRecord rec = ImudFormat::pack(imu);
        imud_file_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));

        // Write IMUDI index record (16 bytes)
        ImudiRecord idx;
        idx.pc_ts_rel = imu.pc_ts_rel;
        idx.position  = file_position_;
        imudi_file_.write(reinterpret_cast<const char*>(&idx), sizeof(idx));

        file_position_ += sizeof(ImudRecord);
    }

    void onStop() override {
        imud_file_.close();
        imudi_file_.close();
    }

private:
    std::string imud_path_;
    std::string imudi_path_;
    std::ofstream imud_file_;
    std::ofstream imudi_file_;
    int64_t file_position_ = 0;
};

} // namespace msl
