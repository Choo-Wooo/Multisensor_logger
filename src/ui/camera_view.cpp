#include "camera_view.h"
#include <imgui.h>

namespace msl {

void CameraView::init() {
    // Texture will be created on first frame update
}

void CameraView::updateFrame(const CameraFrame& frame) {
    if (frame.rgb_data.empty() || frame.width <= 0 || frame.height <= 0) return;
    texture_.update(frame.rgb_data.data(), frame.width, frame.height);
    has_frame_ = true;
}

void CameraView::setDisconnected() {
    has_frame_ = false;
}

void CameraView::render() {
    if (has_frame_ && texture_.isValid()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float tex_aspect = static_cast<float>(texture_.width()) / texture_.height();
        float win_aspect = avail.x / avail.y;

        ImVec2 size;
        if (tex_aspect > win_aspect) {
            size.x = avail.x;
            size.y = avail.x / tex_aspect;
        } else {
            size.y = avail.y;
            size.x = avail.y * tex_aspect;
        }

        // Center the image
        ImVec2 cursor = ImGui::GetCursorPos();
        cursor.x += (avail.x - size.x) * 0.5f;
        cursor.y += (avail.y - size.y) * 0.5f;
        ImGui::SetCursorPos(cursor);

        ImGui::Image((ImTextureID)(uintptr_t)texture_.textureId(), size);
    } else {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 text_size = ImGui::CalcTextSize("Camera: Not Connected");
        ImGui::SetCursorPos(ImVec2(
            (avail.x - text_size.x) * 0.5f,
            (avail.y - text_size.y) * 0.5f
        ));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Camera: Not Connected");
    }
}

} // namespace msl
