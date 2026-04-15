#include "settings_panel.h"
#include "core/clock.h"
#include "core/file_dialog.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// CameraCapture C API for device enumeration (Windows-only)
#ifdef _WIN32
#include <ccap_c.h>
#endif

namespace msl {

void SettingsPanel::render(AppConfig& config, AppState& state) {
    if (!initialized_) {
        std::strncpy(data_dir_buf_, config.data_dir.c_str(), sizeof(data_dir_buf_) - 1);
        initialized_ = true;
    }

    ImGui::BeginChild("SettingsPanel", ImVec2(385, 0), true);

    renderLidarSection(config, state);
    renderRadarSection(config, state);
    renderCameraSection(config, state);
    renderGpsSection(config, state);
    ImGui::Separator();
    renderRecordingSection(config, state);

    ImGui::EndChild();
}

void SettingsPanel::renderStatusIndicator(bool connected) {
    if (connected) {
        ImGui::TextColored(ImVec4(0.30f, 0.69f, 0.31f, 1.0f), "  Connected");
    } else {
        ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.62f, 1.0f), "  Disconnected");
    }
}

void SettingsPanel::setRtspTestResult(bool success, int width, int height, const std::string& message) {
    if (success) {
        native_width_ = width;
        native_height_ = height;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "OK: %dx%d", width, height);
        rtsp_test_status_ = buf;
    } else {
        rtsp_test_status_ = message.empty() ? "Failed" : message;
    }
}

void SettingsPanel::renderLidarSection(AppConfig& config, AppState& state) {
    if (ImGui::CollapsingHeader("Lidar (Ouster)", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool disabled = state.is_recording.load();
        if (disabled) ImGui::BeginDisabled();

        char ip_buf[64];
        std::strncpy(ip_buf, config.lidar_ip.c_str(), sizeof(ip_buf) - 1);
        if (ImGui::InputText("IP##lidar", ip_buf, sizeof(ip_buf)))
            config.lidar_ip = ip_buf;

        ImGui::InputInt("Lidar Port", &config.lidar_port);
        ImGui::InputInt("IMU Port", &config.imu_port);

        const char* btn_label = state.lidar_connected ? "Disconnect##lidar" : "Connect##lidar";
        if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
            if (on_lidar_toggle) on_lidar_toggle();
        }
        renderStatusIndicator(state.lidar_connected);

        if (disabled) ImGui::EndDisabled();
    }
}

void SettingsPanel::renderRadarSection(AppConfig& config, AppState& state) {
    if (ImGui::CollapsingHeader("Radar", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool disabled = state.is_recording.load();
        if (disabled) ImGui::BeginDisabled();

        // SDK selector
        const char* sdk_items[] = {"BSR20", "BSR30"};
        int current = (config.radar_sdk == "BSR30") ? 1 : 0;
        if (ImGui::Combo("SDK##radar", &current, sdk_items, 2)) {
            config.radar_sdk = sdk_items[current];
        }

        char ip_buf[64];
        std::strncpy(ip_buf, config.radar_ip.c_str(), sizeof(ip_buf) - 1);
        if (ImGui::InputText("IP##radar", ip_buf, sizeof(ip_buf)))
            config.radar_ip = ip_buf;

        ImGui::InputInt("TCP Port##radar", &config.radar_port);

        if (config.radar_sdk == "BSR30") {
            ImGui::InputInt("UDP Port##radar", &config.radar_udp_port);
        }

        const char* btn_label = state.radar_connected ? "Disconnect##radar" : "Connect##radar";
        if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
            if (on_radar_toggle) on_radar_toggle();
        }
        renderStatusIndicator(state.radar_connected);

        if (disabled) ImGui::EndDisabled();
    }
}

void SettingsPanel::renderCameraSection(AppConfig& config, AppState& state) {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool disabled = state.is_recording.load();
        if (disabled) ImGui::BeginDisabled();

        // Camera type selector
        const char* type_items[] = {"RTSP", "Webcam"};
        int current = (config.camera_type == "Webcam") ? 1 : 0;
        if (ImGui::Combo("Type##camera", &current, type_items, 2)) {
            config.camera_type = type_items[current];
        }

        if (config.camera_type == "RTSP") {
            char url_buf[512];
            std::strncpy(url_buf, config.camera_rtsp_url.c_str(), sizeof(url_buf) - 1);
            if (ImGui::InputText("URL##camera", url_buf, sizeof(url_buf)))
                config.camera_rtsp_url = url_buf;

            ImGui::SameLine();
            if (ImGui::Button("Test##rtsp")) {
                if (on_rtsp_test) {
                    rtsp_test_status_ = "Testing...";
                    on_rtsp_test(config.camera_rtsp_url);
                }
            }

            if (!rtsp_test_status_.empty()) {
                if (rtsp_test_status_ == "Testing...") {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", rtsp_test_status_.c_str());
                } else if (rtsp_test_status_.find("OK") != std::string::npos) {
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 0.3f, 1.0f), "%s", rtsp_test_status_.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", rtsp_test_status_.c_str());
                }
            }
        } else {
            // Webcam: device list + Find button
#ifdef _WIN32
            if (ImGui::Button("Find##webcam")) {
                webcam_devices_.clear();
                CcapProvider* provider = ccap_provider_create();
                if (provider) {
                    CcapDeviceNamesList list{};
                    if (ccap_provider_find_device_names_list(provider, &list)) {
                        for (size_t i = 0; i < list.deviceCount; ++i) {
                            webcam_devices_.push_back(list.deviceNames[i]);
                        }
                    }
                    ccap_provider_destroy(provider);
                }
                if (webcam_devices_.empty()) {
                    webcam_devices_.push_back("(No devices found)");
                }
            }
            ImGui::SameLine();
#else
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "USB webcam not supported on Linux");
            ImGui::SameLine();
#endif
            if (!webcam_devices_.empty()) {
                std::vector<const char*> items;
                for (auto& d : webcam_devices_) items.push_back(d.c_str());
                if (ImGui::Combo("Device##webcam", &webcam_device_idx_, items.data(), static_cast<int>(items.size()))) {
                    config.camera_webcam_index = webcam_device_idx_;
                }
            } else {
                ImGui::Text("Device: %d", config.camera_webcam_index);
            }
            // Webcam: resolution determined by device, shown as info after connect
            if (state.camera_connected) {
                ImGui::Text("Resolution: %d x %d", config.camera_width, config.camera_height);
            }
        }

        // Resolution preset (RTSP only)
        if (config.camera_type == "RTSP" && native_width_ > 0 && native_height_ > 0) {
            // Build presets based on native aspect ratio
            float aspect = static_cast<float>(native_width_) / native_height_;
            struct Preset { const char* label; int w; int h; };

            Preset presets[4];
            int preset_count = 0;

            // Determine presets based on aspect ratio
            if (std::abs(aspect - 16.0f / 9.0f) < 0.1f) {
                // 16:9
                presets[0] = {"1920 x 1080 (FHD)", 1920, 1080};
                presets[1] = {"2560 x 1440 (QHD)", 2560, 1440};
                presets[2] = {"3840 x 2160 (UHD)", 3840, 2160};
                preset_count = 3;
            } else if (std::abs(aspect - 4.0f / 3.0f) < 0.1f) {
                // 4:3
                presets[0] = {"1600 x 1200 (FHD)", 1600, 1200};
                presets[1] = {"2048 x 1536 (QHD)", 2048, 1536};
                presets[2] = {"3200 x 2400 (UHD)", 3200, 2400};
                preset_count = 3;
            } else {
                // Custom ratio - scale from native
                presets[0] = {"FHD", std::min(native_width_, 1920), static_cast<int>(std::min(native_width_, 1920) / aspect)};
                presets[1] = {"QHD", std::min(native_width_, 2560), static_cast<int>(std::min(native_width_, 2560) / aspect)};
                presets[2] = {"Native", native_width_, native_height_};
                preset_count = 3;
            }

            // Add native resolution if different from presets
            bool native_in_presets = false;
            for (int i = 0; i < preset_count; ++i) {
                if (presets[i].w == native_width_ && presets[i].h == native_height_)
                    native_in_presets = true;
            }
            if (!native_in_presets && preset_count < 4) {
                char native_label[64];
                std::snprintf(native_label, sizeof(native_label), "%d x %d (Native)", native_width_, native_height_);
                presets[preset_count] = {native_label, native_width_, native_height_};
                preset_count++;
            }

            // Find current preset
            int cur_preset = -1;
            for (int i = 0; i < preset_count; ++i) {
                if (presets[i].w == config.camera_width && presets[i].h == config.camera_height) {
                    cur_preset = i;
                    break;
                }
            }
            if (cur_preset < 0) cur_preset = 0;

            const char* labels[4];
            for (int i = 0; i < preset_count; ++i) labels[i] = presets[i].label;

            if (ImGui::Combo("Resolution##camera", &cur_preset, labels, preset_count)) {
                config.camera_width = presets[cur_preset].w;
                config.camera_height = presets[cur_preset].h;
            }
        } else if (config.camera_type == "RTSP") {
            // RTSP: no native info yet — show manual input
            ImGui::InputInt("Width##camera", &config.camera_width);
            ImGui::SameLine();
            ImGui::InputInt("Height##camera", &config.camera_height);
        }

        const char* btn_label = state.camera_connected ? "Disconnect##camera" : "Connect##camera";
        if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
            if (on_camera_toggle) on_camera_toggle();
        }
        renderStatusIndicator(state.camera_connected);

        if (disabled) ImGui::EndDisabled();
    }
}

void SettingsPanel::renderGpsSection(AppConfig& config, AppState& state) {
    if (ImGui::CollapsingHeader("GPS (USB)", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool disabled = state.is_recording.load();
        if (disabled) ImGui::BeginDisabled();

        char port_buf[32];
        std::strncpy(port_buf, config.gps_port.c_str(), sizeof(port_buf) - 1);
        if (ImGui::InputText("Port##gps", port_buf, sizeof(port_buf)))
            config.gps_port = port_buf;
        ImGui::SameLine();
        ImGui::TextDisabled("(empty=auto)");

        const int baud_options[] = {4800, 9600, 19200, 38400, 57600, 115200};
        const char* baud_labels[] = {"4800", "9600", "19200", "38400", "57600", "115200"};
        int baud_idx = 1; // default 9600
        for (int i = 0; i < 6; ++i) {
            if (baud_options[i] == config.gps_baudrate) { baud_idx = i; break; }
        }
        if (ImGui::Combo("Baudrate##gps", &baud_idx, baud_labels, 6)) {
            config.gps_baudrate = baud_options[baud_idx];
        }

        const char* btn_label = state.gps_connected ? "Disconnect##gps" : "Connect##gps";
        if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
            if (on_gps_toggle) on_gps_toggle();
        }
        renderStatusIndicator(state.gps_connected);

        if (disabled) ImGui::EndDisabled();
    }
}

void SettingsPanel::renderRecordingSection(AppConfig& config, AppState& state) {
    ImGui::Text("Recording");
    ImGui::Separator();

    bool recording = state.is_recording.load();

    if (recording) ImGui::BeginDisabled();

    ImGui::InputText("Session Name", session_name_buf_, sizeof(session_name_buf_));
    ImGui::SameLine();
    ImGui::TextDisabled("(empty=auto)");

    ImGui::InputText("Data Directory", data_dir_buf_, sizeof(data_dir_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string selected = FileDialog::selectFolder("Select Data Directory", config.data_dir);
        if (!selected.empty()) {
            std::strncpy(data_dir_buf_, selected.c_str(), sizeof(data_dir_buf_) - 1);
        }
    }
    config.data_dir = data_dir_buf_;

    if (recording) ImGui::EndDisabled();

    ImGui::Spacing();

    if (!recording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.69f, 0.31f, 1.0f));
        if (ImGui::Button("Start Recording", ImVec2(-1, 35))) {
            // Set session name from input or auto-generate
            if (session_name_buf_[0] != '\0') {
                std::lock_guard<std::mutex> lock(state.session_mutex);
                state.session_name = session_name_buf_;
            } else {
                std::lock_guard<std::mutex> lock(state.session_mutex);
                state.session_name = Clock::generateSessionName();
            }
            if (on_start_recording) on_start_recording();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.22f, 0.21f, 1.0f));
        if (ImGui::Button("Stop Recording", ImVec2(-1, 35))) {
            if (on_stop_recording) on_stop_recording();
        }
        ImGui::PopStyleColor();

        // Duration display
        double elapsed = Clock::now() - state.recording_start_time.load();
        ImGui::Text("Duration: %s", Clock::formatDuration(elapsed).c_str());
    }

    // FPS calculation (update every 0.5 seconds)
    double now = Clock::now();
    if (last_fps_time_ == 0.0) last_fps_time_ = now;
    double dt = now - last_fps_time_;
    if (dt >= 0.5) {
        uint64_t l = state.lidar_frames.load(), r = state.radar_frames.load();
        uint64_t c = state.camera_frames.load(), g = state.gps_frames.load();
        uint64_t i = state.imu_frames.load();
        fps_lidar_  = static_cast<float>((l - prev_lidar_) / dt);
        fps_radar_  = static_cast<float>((r - prev_radar_) / dt);
        fps_camera_ = static_cast<float>((c - prev_camera_) / dt);
        fps_gps_    = static_cast<float>((g - prev_gps_) / dt);
        fps_imu_    = static_cast<float>((i - prev_imu_) / dt);
        prev_lidar_ = l; prev_radar_ = r; prev_camera_ = c; prev_gps_ = g; prev_imu_ = i;
        last_fps_time_ = now;
    }

    // Frame counts
    ImGui::Spacing();
    ImGui::Text("Frames: L:%llu R:%llu C:%llu G:%llu I:%llu",
                state.lidar_frames.load(),
                state.radar_frames.load(),
                state.camera_frames.load(),
                state.gps_frames.load(),
                state.imu_frames.load());
    ImGui::Text("FPS:    L:%.1f R:%.1f C:%.1f G:%.1f I:%.1f",
                fps_lidar_, fps_radar_, fps_camera_, fps_gps_, fps_imu_);
}

} // namespace msl
