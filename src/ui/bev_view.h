#pragma once

#include "render/bev_renderer.h"
#include "render/gl_helpers.h"
#include "core/sensor_data.h"
#include <vector>

namespace msl {

/// BEV viewport widget. Renders to FBO and displays via ImGui::Image().
/// Overlays radar track ID labels using ImGui draw list.
class BevView {
public:
    void init();
    void updateLidar(const float* xyz, int num_points);
    void updateRadar(const std::vector<RadarTrack>& tracks);
    void render();  // Call within ImGui context
    void destroy();

private:
    BevRenderer renderer_;
    uint32_t fbo_ = 0;
    uint32_t color_tex_ = 0;
    uint32_t depth_rb_ = 0;
    int fbo_width_ = 0;
    int fbo_height_ = 0;
};

} // namespace msl
