#pragma once

#include "core/sensor_data.h"
#include <vector>
#include <mutex>
#include <string>

namespace msl {

/// Embedded GPS trail viewer rendered directly in ImGui.
/// Shows GPS trajectory on a coordinate grid with zoom/pan.
class GpsMapView {
public:
    GpsMapView() = default;

    void setCenter(double lat, double lon) { center_lat_ = lat; center_lon_ = lon; }

    /// Update with new GPS position.
    void updatePosition(double lat, double lon);

    /// Clear trail.
    void clearTrail();

    /// Render the map widget in ImGui.
    void render();

private:
    struct Point { double lat; double lon; };
    std::vector<Point> trail_;
    std::mutex trail_mutex_;
    int max_trail_ = 2000;

    // Current position
    double cur_lat_ = 0.0;
    double cur_lon_ = 0.0;
    bool has_fix_ = false;

    // View state
    double center_lat_ = 37.5555;
    double center_lon_ = 126.9738;
    double zoom_ = 500.0;  // meters per screen height
    bool auto_center_ = true;

    // Convert lat/lon to local XY (meters from center)
    void latLonToXY(double lat, double lon, float& x, float& y) const;
};

} // namespace msl
