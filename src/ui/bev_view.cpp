#include "bev_view.h"
#include <imgui.h>
#include <glad/glad.h>
#include <cstdio>
#include <cmath>

namespace msl {

void BevView::init() {
    renderer_.init();
}

void BevView::updateLidar(const float* xyz, int num_points) {
    renderer_.updateLidar(xyz, num_points);
}

void BevView::updateRadar(const std::vector<RadarTrack>& tracks) {
    renderer_.updateRadar(tracks);
}

void BevView::render() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y);
    if (w < 1 || h < 1) return;

    // Create or resize FBO
    if (fbo_ == 0 || w != fbo_width_ || h != fbo_height_) {
        if (fbo_) deleteFBO(fbo_, color_tex_, depth_rb_);
        fbo_ = createFBO(w, h, color_tex_, depth_rb_);
        fbo_width_ = w;
        fbo_height_ = h;
    }
    if (!fbo_) return;

    // Render to FBO
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    renderer_.render(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Display FBO texture
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(uintptr_t)color_tex_,
                 ImVec2((float)w, (float)h),
                 ImVec2(0, 1), ImVec2(1, 0));

    // --- Mouse interaction (zoom & pan) ---
    if (ImGui::IsItemHovered()) {
        // Scroll wheel → zoom
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            ImVec2 mouse = ImGui::GetMousePos();
            float mx_ndc = (mouse.x - cursor_pos.x) / w;
            float my_ndc = (mouse.y - cursor_pos.y) / h;
            renderer_.zoom(wheel, mx_ndc, my_ndc);
        }

        // Middle mouse drag → pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            renderer_.pan(delta.x, delta.y, w, h);
        }

        // Right click → reset view
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            renderer_.resetView();
        }
    }

    // --- Grid axis labels ---
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    {
        float view_range = std::max(renderer_.viewXMax() - renderer_.viewXMin(),
                                     renderer_.viewYMax() - renderer_.viewYMin());
        float grid_step = 50.0f;
        if (view_range < 50)        grid_step = 5.0f;
        else if (view_range < 100)  grid_step = 10.0f;
        else if (view_range < 200)  grid_step = 20.0f;
        else if (view_range < 500)  grid_step = 50.0f;
        else                        grid_step = 100.0f;

        // X axis labels (bottom)
        for (float x = std::ceil(renderer_.viewXMin() / grid_step) * grid_step;
             x <= renderer_.viewXMax(); x += grid_step) {
            float sx, sy;
            if (renderer_.worldToScreen(x, renderer_.viewYMin(), w, h, sx, sy)) {
                char label[16];
                std::snprintf(label, sizeof(label), "%.0f", x);
                draw_list->AddText(
                    ImVec2(cursor_pos.x + sx - 10, cursor_pos.y + h - 15),
                    IM_COL32(180, 180, 180, 200), label);
            }
        }

        // Y axis labels (left)
        for (float y = std::ceil(renderer_.viewYMin() / grid_step) * grid_step;
             y <= renderer_.viewYMax(); y += grid_step) {
            float sx, sy;
            if (renderer_.worldToScreen(renderer_.viewXMin(), y, w, h, sx, sy)) {
                char label[16];
                std::snprintf(label, sizeof(label), "%.0f", y);
                draw_list->AddText(
                    ImVec2(cursor_pos.x + 3, cursor_pos.y + sy - 7),
                    IM_COL32(180, 180, 180, 200), label);
            }
        }
    }

    // --- Radar track ID labels (color based on |VY| only — VX is unreliable) ---
    const auto& tracks = renderer_.radarTracks();
    for (const auto& t : tracks) {
        float sx, sy;
        if (renderer_.worldToScreen(t.y_pos, -t.x_pos, w, h, sx, sy)) {
            float speed = std::abs(t.y_vel);  // VY only
            char label[48];
            std::snprintf(label, sizeof(label), "%d vy:%.1f", t.id, t.y_vel);
            ImU32 color = (speed < 5.0f) ? IM_COL32(255, 100, 100, 255)   // red = stationary
                                          : IM_COL32(100, 255, 120, 255);  // green = moving
            draw_list->AddText(
                ImVec2(cursor_pos.x + sx + 8, cursor_pos.y + sy - 8),
                color, label);
        }
    }

    // --- Title ---
    draw_list->AddText(
        ImVec2(cursor_pos.x + w / 2 - 40, cursor_pos.y + 5),
        IM_COL32(200, 200, 200, 255), "Bird's Eye View");

    // --- Axis labels ---
    draw_list->AddText(
        ImVec2(cursor_pos.x + w / 2 - 25, cursor_pos.y + h - 30),
        IM_COL32(150, 150, 150, 180), "Lateral (m)");
    // Vertical label on left
    draw_list->AddText(
        ImVec2(cursor_pos.x + 3, cursor_pos.y + h / 2 - 30),
        IM_COL32(150, 150, 150, 180), "Forward");
    draw_list->AddText(
        ImVec2(cursor_pos.x + 3, cursor_pos.y + h / 2 - 15),
        IM_COL32(150, 150, 150, 180), "(m)");

    // --- Help text ---
    draw_list->AddText(
        ImVec2(cursor_pos.x + w - 200, cursor_pos.y + 5),
        IM_COL32(120, 120, 120, 150), "Scroll:Zoom Mid:Pan R:Reset");
}

void BevView::destroy() {
    if (fbo_) {
        deleteFBO(fbo_, color_tex_, depth_rb_);
        fbo_ = 0; color_tex_ = 0; depth_rb_ = 0;
    }
    renderer_.destroy();
}

} // namespace msl
