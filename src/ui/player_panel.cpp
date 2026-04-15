#include "player_panel.h"
#include "core/clock.h"
#include "core/file_dialog.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace msl {

void PlayerPanel::setSessionInfo(const std::string& name, const std::vector<std::string>& sensors,
                                  int total_frames, double total_duration) {
    session_name_ = name;
    available_sensors_ = sensors;
    total_frames_ = total_frames;
    total_duration_ = total_duration;
    current_frame_ = 0;
    current_time_ = 0.0;
    session_loaded_ = true;
    ref_sensor_idx_ = 0;
}

void PlayerPanel::setCurrentFrame(int frame, double time_sec) {
    current_frame_ = frame;
    current_time_ = time_sec;
}

void PlayerPanel::setPlaying(bool playing) {
    is_playing_ = playing;
}

void PlayerPanel::render(const std::string& default_data_dir) {
    ImGui::BeginChild("PlayerPanel", ImVec2(385, 0), true);

    // --- Load Session ---
    ImGui::Text("Session");
    ImGui::Separator();

    ImGui::InputText("Path##player", path_buf_, sizeof(path_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##player")) {
        std::string browse_dir = default_data_dir;
        if (browse_dir.empty()) browse_dir = ".";
        std::string selected = FileDialog::selectSessionFolder("Load Session", browse_dir);
        if (!selected.empty()) {
            std::strncpy(path_buf_, selected.c_str(), sizeof(path_buf_) - 1);
        }
    }
    if (ImGui::Button("Load Session", ImVec2(-1, 0))) {
        if (on_load_session && path_buf_[0] != '\0') {
            on_load_session(path_buf_);
        }
    }

    if (session_loaded_) {
        ImGui::Text("Session: %s", session_name_.c_str());

        // Available sensors
        std::string sensors_str;
        for (size_t i = 0; i < available_sensors_.size(); ++i) {
            if (i > 0) sensors_str += ", ";
            sensors_str += available_sensors_[i];
        }
        ImGui::Text("Sensors: %s", sensors_str.c_str());

        ImGui::Spacing();

        // --- Reference Sensor ---
        ImGui::Text("Reference Sensor");
        ImGui::Separator();

        if (!available_sensors_.empty()) {
            std::vector<const char*> items;
            for (auto& s : available_sensors_) items.push_back(s.c_str());
            if (ImGui::Combo("Reference##player", &ref_sensor_idx_, items.data(), static_cast<int>(items.size()))) {
                if (on_reference_changed)
                    on_reference_changed(available_sensors_[ref_sensor_idx_]);
            }
        }
        ImGui::Text("Total Frames: %d", total_frames_);

        ImGui::Spacing();

        // --- Playback Controls ---
        ImGui::Text("Playback");
        ImGui::Separator();

        // Transport buttons
        if (ImGui::Button("<<##prev", ImVec2(50, 30))) {
            if (on_prev_frame) on_prev_frame();
        }
        ImGui::SameLine();
        if (!is_playing_) {
            if (ImGui::Button(" > ##play", ImVec2(50, 30))) {
                if (on_play) on_play();
            }
        } else {
            if (ImGui::Button("||##pause", ImVec2(50, 30))) {
                if (on_pause) on_pause();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(">>##next", ImVec2(50, 30))) {
            if (on_next_frame) on_next_frame();
        }

        // Speed buttons
        ImGui::Spacing();
        const float speeds[] = {0.5f, 1.0f, 2.0f, 4.0f};
        const char* speed_labels[] = {"x0.5", "x1.0", "x2.0", "x4.0"};
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine();
            bool selected = (current_speed_ == speeds[i]);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.60f, 0.0f, 1.0f));
            if (ImGui::Button(speed_labels[i], ImVec2(60, 25))) {
                current_speed_ = speeds[i];
                if (on_speed_changed) on_speed_changed(current_speed_);
            }
            if (selected) ImGui::PopStyleColor();
        }

        // Timeline slider
        ImGui::Spacing();
        int frame = current_frame_;
        if (ImGui::SliderInt("##timeline", &frame, 0, total_frames_ > 0 ? total_frames_ - 1 : 0)) {
            if (on_seek) on_seek(frame);
        }

        // Time display
        ImGui::Text("%s / %s",
                    Clock::formatDurationMs(current_time_).c_str(),
                    Clock::formatDurationMs(total_duration_).c_str());
    }

    ImGui::EndChild();
}

} // namespace msl
