#include "gps_map_view.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace msl {

// Earth radius for lat/lon → meter conversion
static constexpr double kEarthRadius = 6378137.0;
static constexpr double kDeg2Rad = 3.14159265358979 / 180.0;

void GpsMapView::latLonToXY(double lat, double lon, float& x, float& y) const {
    // Simple equirectangular projection (meters from center)
    double dlat = (lat - center_lat_) * kDeg2Rad * kEarthRadius;
    double dlon = (lon - center_lon_) * kDeg2Rad * kEarthRadius * std::cos(center_lat_ * kDeg2Rad);
    x = static_cast<float>(dlon);  // East = +X
    y = static_cast<float>(dlat);  // North = +Y
}

void GpsMapView::updatePosition(double lat, double lon) {
    if (lat == 0.0 && lon == 0.0) return;

    std::lock_guard<std::mutex> lock(trail_mutex_);
    trail_.push_back({lat, lon});
    if (static_cast<int>(trail_.size()) > max_trail_) {
        trail_.erase(trail_.begin());
    }
    cur_lat_ = lat;
    cur_lon_ = lon;
    has_fix_ = true;

    if (auto_center_) {
        center_lat_ = lat;
        center_lon_ = lon;
    }
}

void GpsMapView::clearTrail() {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    trail_.clear();
    has_fix_ = false;
}

void GpsMapView::render() {
    if (!ImGui::CollapsingHeader("GPS Map", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float map_h = std::max(avail.y * 0.5f, 120.0f);  // Use half available height
    ImVec2 canvas_size(avail.x, map_h);
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

    // Draw background
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_end(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
    dl->AddRectFilled(canvas_pos, canvas_end, IM_COL32(20, 25, 30, 255));
    dl->AddRect(canvas_pos, canvas_end, IM_COL32(60, 60, 60, 255));

    // Invisible button for mouse interaction
    ImGui::InvisibleButton("##gps_map", canvas_size);
    bool hovered = ImGui::IsItemHovered();

    // Zoom with scroll wheel
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            zoom_ *= (wheel > 0) ? 0.8 : 1.25;
            zoom_ = std::clamp(zoom_, 10.0, 50000.0);
        }

        // Pan with middle mouse
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            double meters_per_px = zoom_ / canvas_size.y;
            center_lon_ -= delta.x * meters_per_px / (kEarthRadius * std::cos(center_lat_ * kDeg2Rad) * kDeg2Rad);
            center_lat_ += delta.y * meters_per_px / (kEarthRadius * kDeg2Rad);
            auto_center_ = false;
        }

        // Right click: re-center + auto follow
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            auto_center_ = true;
            if (has_fix_) {
                center_lat_ = cur_lat_;
                center_lon_ = cur_lon_;
            }
        }
    }

    // Convert helper: world XY (meters) to screen pixel
    float cx = canvas_pos.x + canvas_size.x * 0.5f;
    float cy = canvas_pos.y + canvas_size.y * 0.5f;
    float scale = canvas_size.y / static_cast<float>(zoom_);  // pixels per meter

    auto worldToScreen = [&](float wx, float wy) -> ImVec2 {
        return ImVec2(cx + wx * scale, cy - wy * scale);  // flip Y
    };

    // --- Grid ---
    double grid_step_m = 100.0;
    if (zoom_ < 50) grid_step_m = 5;
    else if (zoom_ < 100) grid_step_m = 10;
    else if (zoom_ < 500) grid_step_m = 50;
    else if (zoom_ < 2000) grid_step_m = 100;
    else if (zoom_ < 10000) grid_step_m = 500;
    else grid_step_m = 2000;

    float half_w = static_cast<float>(zoom_) * canvas_size.x / canvas_size.y * 0.5f;
    float half_h = static_cast<float>(zoom_) * 0.5f;

    for (double g = -half_w - fmod(half_w, grid_step_m); g <= half_w; g += grid_step_m) {
        float gf = static_cast<float>(g);
        ImVec2 p1 = worldToScreen(gf, -half_h);
        ImVec2 p2 = worldToScreen(gf, half_h);
        if (p1.x >= canvas_pos.x && p1.x <= canvas_end.x)
            dl->AddLine(p1, p2, IM_COL32(50, 50, 60, 100));
    }
    for (double g = -half_h - fmod(half_h, grid_step_m); g <= half_h; g += grid_step_m) {
        float gf = static_cast<float>(g);
        ImVec2 p1 = worldToScreen(-half_w, gf);
        ImVec2 p2 = worldToScreen(half_w, gf);
        if (p1.y >= canvas_pos.y && p1.y <= canvas_end.y)
            dl->AddLine(p1, p2, IM_COL32(50, 50, 60, 100));
    }

    // --- Trail ---
    {
        std::lock_guard<std::mutex> lock(trail_mutex_);
        if (trail_.size() >= 2) {
            for (size_t i = 1; i < trail_.size(); ++i) {
                float x1, y1, x2, y2;
                latLonToXY(trail_[i - 1].lat, trail_[i - 1].lon, x1, y1);
                latLonToXY(trail_[i].lat, trail_[i].lon, x2, y2);
                ImVec2 p1 = worldToScreen(x1, y1);
                ImVec2 p2 = worldToScreen(x2, y2);

                // Clip to canvas
                if ((p1.x >= canvas_pos.x - 50 && p1.x <= canvas_end.x + 50) ||
                    (p2.x >= canvas_pos.x - 50 && p2.x <= canvas_end.x + 50)) {
                    dl->AddLine(p1, p2, IM_COL32(255, 152, 0, 200), 2.5f);
                }
            }
        }

        // Current position marker (green filled circle)
        if (has_fix_) {
            float px, py;
            latLonToXY(cur_lat_, cur_lon_, px, py);
            ImVec2 pos = worldToScreen(px, py);
            dl->AddCircleFilled(pos, 6, IM_COL32(76, 175, 80, 255));
            dl->AddCircle(pos, 6, IM_COL32(255, 255, 255, 200), 0, 2.0f);
            dl->AddCircle(pos, 12, IM_COL32(76, 175, 80, 80), 0, 1.5f);
        }
    }

    // --- Labels ---
    char label[64];

    // Coordinates at bottom-left
    if (has_fix_) {
        std::snprintf(label, sizeof(label), "%.6f, %.6f", cur_lat_, cur_lon_);
        dl->AddText(ImVec2(canvas_pos.x + 5, canvas_end.y - 18),
                    IM_COL32(200, 200, 200, 200), label);
    }

    // Scale bar at bottom-right
    float scale_bar_m = static_cast<float>(grid_step_m);
    float scale_bar_px = scale_bar_m * scale;
    if (scale_bar_px > 20 && scale_bar_px < canvas_size.x * 0.4f) {
        ImVec2 bar_start(canvas_end.x - scale_bar_px - 15, canvas_end.y - 12);
        ImVec2 bar_end(canvas_end.x - 15, canvas_end.y - 12);
        dl->AddLine(bar_start, bar_end, IM_COL32(200, 200, 200, 200), 2.0f);
        dl->AddLine(ImVec2(bar_start.x, bar_start.y - 4), ImVec2(bar_start.x, bar_start.y + 4),
                    IM_COL32(200, 200, 200, 200));
        dl->AddLine(ImVec2(bar_end.x, bar_end.y - 4), ImVec2(bar_end.x, bar_end.y + 4),
                    IM_COL32(200, 200, 200, 200));

        if (scale_bar_m >= 1000)
            std::snprintf(label, sizeof(label), "%.0f km", scale_bar_m / 1000);
        else
            std::snprintf(label, sizeof(label), "%.0f m", scale_bar_m);
        dl->AddText(ImVec2(bar_start.x, bar_start.y - 16),
                    IM_COL32(200, 200, 200, 180), label);
    }

    // Auto-center indicator
    if (auto_center_) {
        dl->AddText(ImVec2(canvas_pos.x + 5, canvas_pos.y + 3),
                    IM_COL32(76, 175, 80, 200), "AUTO");
    }

    // Help
    dl->AddText(ImVec2(canvas_end.x - 160, canvas_pos.y + 3),
                IM_COL32(120, 120, 120, 150), "Scroll:Zoom Mid:Pan R:Auto");

    // Trail count
    {
        std::lock_guard<std::mutex> lock(trail_mutex_);
        std::snprintf(label, sizeof(label), "%d pts", static_cast<int>(trail_.size()));
        dl->AddText(ImVec2(canvas_pos.x + 5, canvas_pos.y + 18),
                    IM_COL32(150, 150, 150, 150), label);
    }
}

} // namespace msl
