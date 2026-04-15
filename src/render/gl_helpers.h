#pragma once

#include <string>
#include <cstdint>

namespace msl {

/// Compile a vertex+fragment shader pair and return the program ID.
/// Returns 0 on failure.
uint32_t createShaderProgram(const std::string& vertSrc, const std::string& fragSrc);

/// Create and return a framebuffer object with a color texture and depth renderbuffer.
/// Sets outColorTex to the color attachment texture ID.
/// Returns the FBO ID, or 0 on failure.
uint32_t createFBO(int width, int height, uint32_t& outColorTex, uint32_t& outDepthRB);

/// Resize an existing FBO's attachments.
void resizeFBO(uint32_t fbo, uint32_t colorTex, uint32_t depthRB, int width, int height);

/// Delete FBO and its attachments.
void deleteFBO(uint32_t fbo, uint32_t colorTex, uint32_t depthRB);

} // namespace msl
