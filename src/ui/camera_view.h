#pragma once

#include "render/camera_texture.h"
#include "core/sensor_data.h"

namespace msl {

/// Camera display widget using ImGui::Image with an OpenGL texture.
class CameraView {
public:
    void init();
    void updateFrame(const CameraFrame& frame);
    void setDisconnected();
    void render();  // Call within ImGui context

private:
    CameraTexture texture_;
    bool has_frame_ = false;
};

} // namespace msl
