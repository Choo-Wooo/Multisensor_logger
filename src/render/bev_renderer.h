#pragma once

#include <vector>
#include <cstdint>
#include "core/sensor_data.h"

namespace msl {

/// OpenGL-based Bird's Eye View renderer.
class BevRenderer {
public:
    BevRenderer() = default;
    ~BevRenderer();

    void init();
    void updateLidar(const float* xyz, int num_points);
    void updateRadar(const std::vector<RadarTrack>& tracks);
    void setDetectLine(float y_pos) { detect_line_y_ = y_pos; }

    /// Render BEV to current FBO.
    void render(int width, int height);
    void destroy();

    /// Get radar tracks for label overlay.
    const std::vector<RadarTrack>& radarTracks() const { return radar_tracks_; }

    /// Convert world coords to screen coords.
    bool worldToScreen(float wx, float wy, int vp_width, int vp_height,
                       float& sx, float& sy) const;

    // --- View control (zoom & pan) ---

    /// Zoom in/out by factor around center. delta > 0 = zoom in.
    void zoom(float delta, float mouse_x_ndc = 0.5f, float mouse_y_ndc = 0.5f);

    /// Pan view by pixel delta (screen space).
    void pan(float dx_pixels, float dy_pixels, int vp_width, int vp_height);

    /// Reset view to default.
    void resetView();

    // Current view bounds (read-only for label positioning)
    float viewXMin() const { return x_min_; }
    float viewXMax() const { return x_max_; }
    float viewYMin() const { return y_min_; }
    float viewYMax() const { return y_max_; }

private:
    // Shaders
    uint32_t point_shader_ = 0;
    uint32_t radar_shader_ = 0;  // X marker shader

    // Lidar
    uint32_t lidar_vao_ = 0;
    uint32_t lidar_vbo_ = 0;
    int lidar_point_count_ = 0;

    // Radar
    uint32_t radar_vao_ = 0;
    uint32_t radar_vbo_ = 0;
    int radar_point_count_ = 0;
    std::vector<RadarTrack> radar_tracks_;

    // Grid lines
    uint32_t grid_vao_ = 0;
    uint32_t grid_vbo_ = 0;
    int grid_line_count_ = 0;

    // Detection line
    uint32_t det_line_vao_ = 0;
    uint32_t det_line_vbo_ = 0;
    float detect_line_y_ = 30.0f;

    // View (orthographic)
    float x_min_ = -40.0f, x_max_ = 40.0f;
    float y_min_ = -20.0f, y_max_ = 20.0f;

    // Default view for reset
    static constexpr float kDefXMin = -40.0f, kDefXMax = 40.0f;
    static constexpr float kDefYMin = -20.0f, kDefYMax = 20.0f;

    // Z filter
    float z_min_ = -20.0f, z_max_ = 10.0f;
    int max_points_ = 50000;

    void createShaders();
    void createBuffers();
    void updateGridLines();
    void buildProjectionMatrix(float* proj) const;
};

} // namespace msl
