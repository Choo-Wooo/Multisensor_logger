#include "sensor_info_view.h"
#include <imgui.h>
#include <cstdio>

namespace msl {

void SensorInfoView::updateGps(const GpsFix& fix) {
    last_gps_ = fix;
    has_gps_ = true;

    // Update map trail
    if (fix.valid && fix.latitude != 0.0 && fix.longitude != 0.0) {
        map_view_.updatePosition(fix.latitude, fix.longitude);
    }
}

void SensorInfoView::updateImu(const ImuData& imu) {
    last_imu_ = imu;
    has_imu_ = true;
}

void SensorInfoView::setMapCenter(double origin_lat, double origin_lon) {
    map_view_.setCenter(origin_lat, origin_lon);
}

void SensorInfoView::render() {
    // GPS Info
    if (ImGui::CollapsingHeader("GPS Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (has_gps_) {
            ImGui::Text("Latitude:   %12.7f", last_gps_.latitude);
            ImGui::Text("Longitude:  %12.7f", last_gps_.longitude);
            ImGui::Text("Altitude:   %8.2f m", last_gps_.altitude);

            const char* fix_str = "No Fix";
            switch (last_gps_.fix_quality) {
                case 1: fix_str = "GPS";       break;
                case 2: fix_str = "DGPS";      break;
                case 4: fix_str = "RTK Fixed"; break;
                case 5: fix_str = "RTK Float"; break;
            }
            ImGui::Text("Fix:        %s", fix_str);
            ImGui::Text("Satellites: %d", last_gps_.satellites);
            ImGui::Text("HDOP:       %.1f", last_gps_.hdop);
            ImGui::Text("Speed:      %.1f km/h", last_gps_.speed_kmh);
            ImGui::Text("Heading:    %.1f deg", last_gps_.heading);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No GPS data");
        }
    }

    // GPS Map
    map_view_.render();

    ImGui::Separator();

    // IMU Info
    if (ImGui::CollapsingHeader("IMU Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (has_imu_) {
            ImGui::Text("Accel X: %+8.4f G", last_imu_.accel[0]);
            ImGui::Text("Accel Y: %+8.4f G", last_imu_.accel[1]);
            ImGui::Text("Accel Z: %+8.4f G", last_imu_.accel[2]);
            ImGui::Separator();
            ImGui::Text("Gyro  X: %+8.2f deg/s", last_imu_.gyro[0]);
            ImGui::Text("Gyro  Y: %+8.2f deg/s", last_imu_.gyro[1]);
            ImGui::Text("Gyro  Z: %+8.2f deg/s", last_imu_.gyro[2]);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No IMU data");
        }
    }
}

} // namespace msl
