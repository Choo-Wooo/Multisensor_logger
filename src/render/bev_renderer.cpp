#include "bev_renderer.h"
#include "gl_helpers.h"

#include <glad/glad.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>
#include <vector>

namespace msl {

// --- Shaders ---
static const char* kVert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uProj;
uniform float uPointSize;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    gl_PointSize = uPointSize;
}
)";

static const char* kFrag = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = uColor;
}
)";

// Radar X marker fragment shader
static const char* kRadarFrag = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float d = min(abs(p.x - p.y), abs(p.x + p.y));
    if (d > 0.35) discard;
    FragColor = uColor;
}
)";

BevRenderer::~BevRenderer() { destroy(); }

void BevRenderer::init() {
    createShaders();
    createBuffers();
}

void BevRenderer::createShaders() {
    point_shader_ = createShaderProgram(kVert, kFrag);
    radar_shader_ = createShaderProgram(kVert, kRadarFrag);
}

void BevRenderer::createBuffers() {
    // Lidar
    glGenVertexArrays(1, &lidar_vao_);
    glGenBuffers(1, &lidar_vbo_);
    glBindVertexArray(lidar_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, lidar_vbo_);
    glBufferData(GL_ARRAY_BUFFER, max_points_ * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Radar
    glGenVertexArrays(1, &radar_vao_);
    glGenBuffers(1, &radar_vbo_);
    glBindVertexArray(radar_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, radar_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 256 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Grid
    glGenVertexArrays(1, &grid_vao_);
    glGenBuffers(1, &grid_vbo_);
    updateGridLines();

    // Detection line
    glGenVertexArrays(1, &det_line_vao_);
    glGenBuffers(1, &det_line_vbo_);
    glBindVertexArray(det_line_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, det_line_vbo_);
    float dummy[4] = {0};
    glBufferData(GL_ARRAY_BUFFER, sizeof(dummy), dummy, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void BevRenderer::updateGridLines() {
    // Generate grid lines covering a large area (-300..300 range)
    std::vector<float> verts;
    float grid_step = 50.0f;  // meters per grid cell

    // Determine visible grid step based on zoom level
    float view_range = std::max(x_max_ - x_min_, y_max_ - y_min_);
    if (view_range < 50)        grid_step = 5.0f;
    else if (view_range < 100)  grid_step = 10.0f;
    else if (view_range < 200)  grid_step = 20.0f;
    else if (view_range < 500)  grid_step = 50.0f;
    else                        grid_step = 100.0f;

    float lo = -500.0f, hi = 500.0f;

    // Vertical lines (X = const)
    for (float x = std::ceil(lo / grid_step) * grid_step; x <= hi; x += grid_step) {
        verts.push_back(x); verts.push_back(lo);
        verts.push_back(x); verts.push_back(hi);
    }
    // Horizontal lines (Y = const)
    for (float y = std::ceil(lo / grid_step) * grid_step; y <= hi; y += grid_step) {
        verts.push_back(lo); verts.push_back(y);
        verts.push_back(hi); verts.push_back(y);
    }

    grid_line_count_ = static_cast<int>(verts.size()) / 2;

    glBindVertexArray(grid_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void BevRenderer::buildProjectionMatrix(float* proj) const {
    std::memset(proj, 0, 16 * sizeof(float));
    float dx = x_max_ - x_min_;
    float dy = y_max_ - y_min_;
    proj[0]  =  2.0f / dx;
    proj[5]  =  2.0f / dy;
    proj[10] = -1.0f;
    proj[12] = -(x_max_ + x_min_) / dx;
    proj[13] = -(y_max_ + y_min_) / dy;
    proj[15] =  1.0f;
}

void BevRenderer::updateLidar(const float* xyz, int num_points) {
    if (!lidar_vbo_ || !xyz || num_points <= 0) {
        lidar_point_count_ = 0;
        return;
    }

    std::vector<float> xy;
    xy.reserve(std::min(num_points, max_points_) * 2);

    for (int i = 0; i < num_points; ++i) {
        float x = xyz[i * 3 + 0];
        float y = xyz[i * 3 + 1];
        float z = xyz[i * 3 + 2];
        if (x == 0.0f && y == 0.0f && z == 0.0f) continue;
        if (z < z_min_ || z > z_max_) continue;
        xy.push_back(x);
        xy.push_back(y);
    }

    int filtered = static_cast<int>(xy.size()) / 2;
    if (filtered > max_points_) {
        std::mt19937 rng(42);
        for (int i = 0; i < max_points_; ++i) {
            int j = i + rng() % (filtered - i);
            std::swap(xy[i * 2], xy[j * 2]);
            std::swap(xy[i * 2 + 1], xy[j * 2 + 1]);
        }
        filtered = max_points_;
    }

    lidar_point_count_ = filtered;
    glBindBuffer(GL_ARRAY_BUFFER, lidar_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, filtered * 2 * sizeof(float), xy.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void BevRenderer::updateRadar(const std::vector<RadarTrack>& tracks) {
    radar_tracks_ = tracks;
    if (!radar_vbo_ || tracks.empty()) {
        radar_point_count_ = 0;
        return;
    }

    std::vector<float> xy;
    xy.reserve(tracks.size() * 2);
    for (const auto& t : tracks) {
        xy.push_back(t.y_pos);
        xy.push_back(-t.x_pos);
    }

    radar_point_count_ = static_cast<int>(tracks.size());
    glBindBuffer(GL_ARRAY_BUFFER, radar_vbo_);
    if (radar_point_count_ * 2 * sizeof(float) > 256 * 2 * sizeof(float))
        glBufferData(GL_ARRAY_BUFFER, radar_point_count_ * 2 * sizeof(float), xy.data(), GL_DYNAMIC_DRAW);
    else
        glBufferSubData(GL_ARRAY_BUFFER, 0, radar_point_count_ * 2 * sizeof(float), xy.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void BevRenderer::render(int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float proj[16];
    buildProjectionMatrix(proj);
    GLint loc_proj, loc_color, loc_ps;

    if (!point_shader_) return;
    glUseProgram(point_shader_);
    loc_proj  = glGetUniformLocation(point_shader_, "uProj");
    loc_color = glGetUniformLocation(point_shader_, "uColor");
    loc_ps    = glGetUniformLocation(point_shader_, "uPointSize");
    glUniformMatrix4fv(loc_proj, 1, GL_FALSE, proj);

    // 1. Grid lines (dark gray)
    if (grid_line_count_ > 0) {
        glUniform4f(loc_color, 0.25f, 0.25f, 0.25f, 0.5f);
        glUniform1f(loc_ps, 1.0f);
        glBindVertexArray(grid_vao_);
        glDrawArrays(GL_LINES, 0, grid_line_count_);
        glBindVertexArray(0);
    }

    // 2. Origin axes (brighter)
    // X axis (Y=0)
    {
        float axis[4] = {x_min_, 0.0f, x_max_, 0.0f};
        glBindBuffer(GL_ARRAY_BUFFER, det_line_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(axis), axis);
        glUniform4f(loc_color, 0.4f, 0.4f, 0.4f, 0.7f);
        glBindVertexArray(det_line_vao_);
        glDrawArrays(GL_LINES, 0, 2);
    }
    // Y axis (X=0)
    {
        float axis[4] = {0.0f, y_min_, 0.0f, y_max_};
        glBindBuffer(GL_ARRAY_BUFFER, det_line_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(axis), axis);
        glDrawArrays(GL_LINES, 0, 2);
        glBindVertexArray(0);
    }

    // 3. Detection line (yellow dashed)
    {
        float line[4] = {x_min_, detect_line_y_, x_max_, detect_line_y_};
        glBindBuffer(GL_ARRAY_BUFFER, det_line_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(line), line);
        glUniform4f(loc_color, 1.0f, 1.0f, 0.0f, 0.6f);
        glBindVertexArray(det_line_vao_);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, 2);
        glBindVertexArray(0);
    }

    // Scale point sizes based on zoom level
    float view_range = std::max(x_max_ - x_min_, y_max_ - y_min_);
    float zoom_factor = 80.0f / view_range;  // 1.0 at default view (80m range)
    float lidar_ps = std::clamp(0.6f * zoom_factor, 0.5f, 2.0f);
    float radar_ps = std::clamp(4.0f * zoom_factor, 4.0f, 12.0f);

    // 4. Lidar points (bright white)
    if (lidar_point_count_ > 0) {
        glUniform4f(loc_color, 1.0f, 1.0f, 1.0f, 0.8f);
        glUniform1f(loc_ps, lidar_ps);
        glBindVertexArray(lidar_vao_);
        glDrawArrays(GL_POINTS, 0, lidar_point_count_);
        glBindVertexArray(0);
    }

    // 5. Radar markers — per-track color based on speed (stationary=red, moving=green)
    // Draw each track individually with its own color
    if (radar_point_count_ > 0 && radar_shader_) {
        glUseProgram(radar_shader_);
        glUniformMatrix4fv(glGetUniformLocation(radar_shader_, "uProj"), 1, GL_FALSE, proj);
        GLint rc_loc = glGetUniformLocation(radar_shader_, "uColor");
        glUniform1f(glGetUniformLocation(radar_shader_, "uPointSize"), radar_ps);

        glBindVertexArray(radar_vao_);
        for (int i = 0; i < radar_point_count_ && i < static_cast<int>(radar_tracks_.size()); ++i) {
            float speed = std::sqrt(radar_tracks_[i].x_vel * radar_tracks_[i].x_vel +
                                     radar_tracks_[i].y_vel * radar_tracks_[i].y_vel);
            if (speed < 5.0f) {  // stationary threshold
                // Stationary — red
                glUniform4f(rc_loc, 1.0f, 0.2f, 0.2f, 1.0f);
            } else {
                // Moving — green
                glUniform4f(rc_loc, 0.2f, 1.0f, 0.3f, 1.0f);
            }
            glDrawArrays(GL_POINTS, i, 1);
        }
        glBindVertexArray(0);
    }

    glUseProgram(0);
    glDisable(GL_BLEND);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

bool BevRenderer::worldToScreen(float wx, float wy, int vp_width, int vp_height,
                                 float& sx, float& sy) const {
    float nx = (wx - x_min_) / (x_max_ - x_min_);
    float ny = (wy - y_min_) / (y_max_ - y_min_);
    if (nx < -0.1f || nx > 1.1f || ny < -0.1f || ny > 1.1f) return false;
    sx = nx * vp_width;
    sy = (1.0f - ny) * vp_height;
    return true;
}

void BevRenderer::zoom(float delta, float mx_ndc, float my_ndc) {
    float factor = (delta > 0) ? 0.9f : 1.1f;

    float cx = x_min_ + (x_max_ - x_min_) * mx_ndc;
    float cy = y_min_ + (y_max_ - y_min_) * (1.0f - my_ndc);  // flip Y

    x_min_ = cx + (x_min_ - cx) * factor;
    x_max_ = cx + (x_max_ - cx) * factor;
    y_min_ = cy + (y_min_ - cy) * factor;
    y_max_ = cy + (y_max_ - cy) * factor;

    updateGridLines();
}

void BevRenderer::pan(float dx_pixels, float dy_pixels, int vp_width, int vp_height) {
    float dx_world = -(dx_pixels / vp_width) * (x_max_ - x_min_);
    float dy_world =  (dy_pixels / vp_height) * (y_max_ - y_min_);

    x_min_ += dx_world;
    x_max_ += dx_world;
    y_min_ += dy_world;
    y_max_ += dy_world;
}

void BevRenderer::resetView() {
    x_min_ = kDefXMin; x_max_ = kDefXMax;
    y_min_ = kDefYMin; y_max_ = kDefYMax;
    updateGridLines();
}

void BevRenderer::destroy() {
    auto del = [](uint32_t& vao, uint32_t& vbo) {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    };
    del(lidar_vao_, lidar_vbo_);
    del(radar_vao_, radar_vbo_);
    del(grid_vao_, grid_vbo_);
    del(det_line_vao_, det_line_vbo_);
    if (point_shader_) { glDeleteProgram(point_shader_); point_shader_ = 0; }
    if (radar_shader_) { glDeleteProgram(radar_shader_); radar_shader_ = 0; }
}

} // namespace msl
