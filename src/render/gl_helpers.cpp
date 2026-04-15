#include "gl_helpers.h"

#include <glad/glad.h>
#include <spdlog/spdlog.h>

namespace msl {

uint32_t createShaderProgram(const std::string& vertSrc, const std::string& fragSrc) {
    auto compile = [](GLenum type, const std::string& src) -> GLuint {
        GLuint shader = glCreateShader(type);
        const char* c = src.c_str();
        glShaderSource(shader, 1, &c, nullptr);
        glCompileShader(shader);

        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            spdlog::error("Shader compile error: {}", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        spdlog::error("Shader link error: {}", log);
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

uint32_t createFBO(int width, int height, uint32_t& outColorTex, uint32_t& outDepthRB) {
    GLuint fbo, tex, rbo;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color texture
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Framebuffer is not complete!");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        glDeleteRenderbuffers(1, &rbo);
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    outColorTex = tex;
    outDepthRB = rbo;
    return fbo;
}

void resizeFBO(uint32_t fbo, uint32_t colorTex, uint32_t depthRB, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
}

void deleteFBO(uint32_t fbo, uint32_t colorTex, uint32_t depthRB) {
    if (fbo)      glDeleteFramebuffers(1, &fbo);
    if (colorTex) glDeleteTextures(1, &colorTex);
    if (depthRB)  glDeleteRenderbuffers(1, &depthRB);
}

} // namespace msl
