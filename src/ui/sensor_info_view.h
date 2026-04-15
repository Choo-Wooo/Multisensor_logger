#pragma once

#include "core/sensor_data.h"
#include "ui/gps_map_view.h"

namespace msl {

/// GPS/IMU telemetry text display widget.
class SensorInfoView {
public:
    void updateGps(const GpsFix& fix);
    void updateImu(const ImuData& imu);
    void setMapCenter(double origin_lat, double origin_lon);
    void render();  // Call within ImGui context

private:
    GpsFix  last_gps_;
    ImuData last_imu_;
    bool    has_gps_ = false;
    bool    has_imu_ = false;
    GpsMapView map_view_;
};

} // namespace msl
