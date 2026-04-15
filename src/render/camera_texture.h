#pragma once

#include <cstdint>

namespace msl {

/// Manages a single OpenGL texture for displaying camera frames.
/// Upload RGB data → display via ImGui::Image().
class CameraTexture {
public:
    CameraTexture() = default;
    ~CameraTexture();

    CameraTexture(const CameraTexture&) = delete;
    CameraTexture& operator=(const CameraTexture&) = delete;

    /// Initialize the texture (call after GL context is ready).
    void init(int width = 1920, int height = 1080);

    /// Upload a new RGB24 frame. Resizes texture if dimensions changed.
    void update(const uint8_t* rgb_data, int width, int height);

    /// Get the GL texture ID for use with ImGui::Image().
    uint32_t textureId() const { return tex_id_; }

    /// Get current dimensions.
    int width()  const { return width_; }
    int height() const { return height_; }

    /// Check if initialized.
    bool isValid() const { return tex_id_ != 0; }

    /// Release GL resources.
    void destroy();

private:
    uint32_t tex_id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace msl
