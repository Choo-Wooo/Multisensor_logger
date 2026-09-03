#include "camera_texture.h"

#include <glad/glad.h>
#include "gl_helpers.h"

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


namespace {

// I420 3-plane -> RGB 변환 셰이더 (BT.601 limited range)
const char* kYuvVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    // FBO 는 좌하단 원점, ImGui 는 좌상단 원점이라 세로를 뒤집어 샘플링한다.
    vUv = vec2((aPos.x + 1.0) * 0.5, (aPos.y + 1.0) * 0.5);
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kYuvFrag = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
uniform bool fullRange;      // yuvj420p (0~255) 이면 true
void main() {
    float y = texture(texY, vUv).r;
    float u = texture(texU, vUv).r - 0.5;
    float v = texture(texV, vUv).r - 0.5;
    if (!fullRange) y = (y - 0.0625) * 1.164383;
    vec3 rgb = vec3(y + 1.596027 * v,
                    y - 0.391762 * u - 0.812968 * v,
                    y + 2.017232 * u);
    FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

} // namespace

bool CameraTexture::ensureYuvResources(int width, int height) {
    if (yuv_prog_ == 0) {
        yuv_prog_ = createShaderProgram(kYuvVert, kYuvFrag);
        if (!yuv_prog_) return false;

        // 풀스크린 삼각형 2개
        static const float verts[] = {
            -1.f, -1.f,  1.f, -1.f,  1.f, 1.f,
            -1.f, -1.f,  1.f,  1.f, -1.f, 1.f,
        };
        glGenVertexArrays(1, &yuv_vao_);
        glGenBuffers(1, &yuv_vbo_);
        glBindVertexArray(yuv_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, yuv_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        glGenTextures(3, yuv_tex_);
        for (int i = 0; i < 3; ++i) {
            glBindTexture(GL_TEXTURE_2D, yuv_tex_[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &yuv_fbo_);
    }

    if (yuv_width_ != width || yuv_height_ != height) {
        yuv_width_ = width;
        yuv_height_ = height;

        // 평면 텍스처 재할당 (U/V 는 절반 크기)
        for (int i = 0; i < 3; ++i) {
            const int w = (i == 0) ? width : width / 2;
            const int h = (i == 0) ? height : height / 2;
            glBindTexture(GL_TEXTURE_2D, yuv_tex_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        // 결과 RGB 텍스처 (ImGui 가 그대로 사용)
        if (!tex_id_) glGenTextures(1, &tex_id_);
        glBindTexture(GL_TEXTURE_2D, tex_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        width_ = width;
        height_ = height;
    }
    return true;
}

void CameraTexture::updateYuv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int width, int height, bool full_range) {
    if (!y || !u || !v || width <= 0 || height <= 0) return;
    if (!ensureYuvResources(width, height)) return;

    const uint8_t* planes[3] = {y, u, v};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < 3; ++i) {
        const int w = (i == 0) ? width : width / 2;
        const int h = (i == 0) ? height : height / 2;
        glBindTexture(GL_TEXTURE_2D, yuv_tex_[i]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, planes[i]);
    }

    // 현재 GL 상태 저장 (ImGui 렌더 루프 중간에 끼어들기 때문)
    GLint prev_fbo = 0, prev_prog = 0, prev_vao = 0;
    GLint prev_viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, yuv_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_id_, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(yuv_prog_);
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, yuv_tex_[i]);
    }
    glUniform1i(glGetUniformLocation(yuv_prog_, "texY"), 0);
    glUniform1i(glGetUniformLocation(yuv_prog_, "texU"), 1);
    glUniform1i(glGetUniformLocation(yuv_prog_, "texV"), 2);
    glUniform1i(glGetUniformLocation(yuv_prog_, "fullRange"), full_range ? 1 : 0);
    glBindVertexArray(yuv_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 상태 복원
    glBindVertexArray(prev_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(prev_prog);
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    if (prev_blend) glEnable(GL_BLEND);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
}

void CameraTexture::destroyYuvResources() {
    if (yuv_tex_[0]) { glDeleteTextures(3, yuv_tex_); yuv_tex_[0] = yuv_tex_[1] = yuv_tex_[2] = 0; }
    if (yuv_fbo_)  { glDeleteFramebuffers(1, &yuv_fbo_); yuv_fbo_ = 0; }
    if (yuv_vbo_)  { glDeleteBuffers(1, &yuv_vbo_); yuv_vbo_ = 0; }
    if (yuv_vao_)  { glDeleteVertexArrays(1, &yuv_vao_); yuv_vao_ = 0; }
    if (yuv_prog_) { glDeleteProgram(yuv_prog_); yuv_prog_ = 0; }
    yuv_width_ = yuv_height_ = 0;
}

void CameraTexture::destroy() {
    destroyYuvResources();
    if (tex_id_) {
        glDeleteTextures(1, &tex_id_);
        tex_id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

} // namespace msl
