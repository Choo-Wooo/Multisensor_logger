#include "camera_texture.h"

#include <glad/glad.h>

namespace msl {

CameraTexture::~CameraTexture() {
    destroy();
}

void CameraTexture::init(int width, int height) {
    destroy();

    width_ = width;
    height_ = height;

    glGenTextures(1, &tex_id_);
    glBindTexture(GL_TEXTURE_2D, tex_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraTexture::update(const uint8_t* rgb_data, int width, int height) {
    if (!tex_id_) {
        init(width, height);
    }

    // Resize if dimensions changed
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glBindTexture(GL_TEXTURE_2D, tex_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_data);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    // Fast sub-image update
    glBindTexture(GL_TEXTURE_2D, tex_id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraTexture::destroy() {
    if (tex_id_) {
        glDeleteTextures(1, &tex_id_);
        tex_id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

} // namespace msl
