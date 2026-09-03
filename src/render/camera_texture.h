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

    /// I420(YUV420 planar) 프레임 업로드.
    /// Y/U/V 를 각각 GL_R8 텍스처로 올리고, 작은 셰이더 패스로 RGB 텍스처에
    /// 변환해 둔다. CPU 색공간 변환이 없고 업로드량도 RGB 대비 절반이다.
    void updateYuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                   int width, int height, bool full_range = false);

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

    // --- YUV -> RGB 변환용 GL 리소스 (updateYuv 사용 시에만 생성) ---
    uint32_t yuv_tex_[3] = {0, 0, 0};
    uint32_t yuv_prog_ = 0;
    uint32_t yuv_fbo_ = 0;
    uint32_t yuv_vao_ = 0;
    uint32_t yuv_vbo_ = 0;
    int yuv_width_ = 0;
    int yuv_height_ = 0;

    bool ensureYuvResources(int width, int height);
    void destroyYuvResources();
};

} // namespace msl
