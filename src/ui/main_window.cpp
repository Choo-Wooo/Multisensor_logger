#include "main_window.h"
#include "core/logging.h"
#include "core/clock.h"
#include "core/file_dialog.h"
#include "loggers/session_logger.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <fstream>
#include <vector>
#include <cstdio>

namespace msl {

void MainWindow::init() {
    // User-writable config path: %APPDATA%\MultisensorLogger\config.ini (Windows)
    // or ~/.config/MultisensorLogger/config.ini (Linux).
    // Fall back to legacy "config.ini" beside the exe so existing setups keep
    // working on first launch (we'll re-save to the new location on shutdown).
    const std::string config_path = FileDialog::getConfigPath();
    if (!config_.load(config_path)) {
        // Try legacy location (next to the exe) as a one-time migration.
        config_.load("config.ini");
    }

    // Default data directory: %USERPROFILE%\Documents\MultiSensorData
    // (avoids UAC issues that arise when writing under Program Files).
    if (config_.data_dir.empty() ||
        config_.data_dir == "Data" ||
        config_.data_dir == FileDialog::getExecutableDir() + "/Data") {
        config_.data_dir = FileDialog::getDefaultDataDir();
    }

    for (auto& v : camera_views_) v.init();
    bev_view_.init();
    sensor_info_view_.setMapCenter(config_.origin_lat, config_.origin_lon);
    connectSettingsCallbacks();
    connectPlayerCallbacks();
    msl::log::ui()->info("MainWindow initialized");
}

void MainWindow::connectSettingsCallbacks() {
    settings_panel_.on_lidar_toggle    = [this]() { toggleLidar(); };
    settings_panel_.on_radar_toggle    = [this]() { toggleRadar(); };
    settings_panel_.on_radar_start_toggle = [this]() { toggleRadarStreaming(); };
    settings_panel_.on_camera_toggle   = [this](int idx) { toggleCamera(idx); };
    settings_panel_.on_gps_toggle      = [this]() { toggleGps(); };
    settings_panel_.on_start_recording = [this]() { startRecording(); };
    settings_panel_.on_stop_recording  = [this]() { stopRecording(); };

    // RTSP Test: connect briefly, get first frame resolution, disconnect
    settings_panel_.on_rtsp_test = [this](int idx, const std::string& url) {
        std::thread([this, idx, url]() {
            try {
                auto test_worker = std::make_unique<CameraRtspWorker>(event_bus_, url, 0, 0);
                std::atomic<bool> got_frame{false};
                int detected_w = 0, detected_h = 0;

                test_worker->on_frame_ready = [&](const CameraFrame& f) {
                    if (!got_frame) {
                        detected_w = f.width;
                        detected_h = f.height;
                        got_frame = true;
                    }
                };

                test_worker->start();

                // Wait up to 10 seconds for first frame
                for (int i = 0; i < 100 && !got_frame; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                test_worker->signalEventLoopExit();
                test_worker->stop();
                test_worker.reset();

                if (got_frame) {
                    event_bus_.post([this, idx, detected_w, detected_h]() {
                        settings_panel_.setRtspTestResult(idx, true, detected_w, detected_h, "");
                        config_.cameras[idx].width = detected_w;
                        config_.cameras[idx].height = detected_h;
                    });
                } else {
                    event_bus_.post([this, idx]() {
                        settings_panel_.setRtspTestResult(idx, false, 0, 0, "Timeout: no frame received");
                    });
                }
            } catch (const std::exception& e) {
                std::string err = e.what();
                event_bus_.post([this, idx, err]() {
                    settings_panel_.setRtspTestResult(idx, false, 0, 0, err);
                });
            }
        }).detach();
    };
}

void MainWindow::connectPlayerCallbacks() {
    player_panel_.on_load_session = [this](const std::string& path) {
        msl::log::ui()->info("Loading session: {}", path);

        session_player_ = std::make_unique<SessionPlayer>(event_bus_);

        // Wire player data callbacks to UI
        session_player_->on_camera_frame = [this](int idx, const CameraFrame& f) {
            if (idx >= 0 && idx < kNumCameras) camera_views_[idx].updateFrame(f);
        };
        session_player_->on_lidar_xyz = [this](const std::vector<float>& xyz, int n) {
            if (n > 0) bev_view_.updateLidar(xyz.data(), n);
        };
        session_player_->on_radar_tracks = [this](const std::vector<RadarTrack>& tracks) {
            bev_view_.updateRadar(tracks);
        };
        session_player_->on_imu_data = [this](const ImuData& imu) {
            sensor_info_view_.updateImu(imu);
        };
        session_player_->on_gps_data = [this](const GpsFix& fix) {
            sensor_info_view_.updateGps(fix);
        };
        session_player_->on_frame_changed = [this](int frame, double time_sec) {
            player_panel_.setCurrentFrame(frame, time_sec);
        };
        session_player_->on_state_changed = [this](bool playing) {
            player_panel_.setPlaying(playing);
        };

        if (session_player_->loadSession(path)) {
            auto& data = session_player_->sessionData();
            player_panel_.setSessionInfo(
                data.config.session_name,
                data.availableSensors(),
                session_player_->totalFrames(),
                data.total_duration
            );
            // Seek to first frame
            session_player_->seekToFrame(0);
            msl::log::ui()->info("Session loaded: {} ({} frames, {:.1f}s)",
                         data.config.session_name,
                         session_player_->totalFrames(),
                         data.total_duration);
        } else {
            msl::log::ui()->error("Failed to load session: {}", path);
            session_player_.reset();
        }
    };

    player_panel_.on_play = [this]() {
        if (session_player_) session_player_->play();
    };
    player_panel_.on_pause = [this]() {
        if (session_player_) session_player_->pause();
    };
    player_panel_.on_next_frame = [this]() {
        if (session_player_) session_player_->nextFrame();
    };
    player_panel_.on_prev_frame = [this]() {
        if (session_player_) session_player_->prevFrame();
    };
    player_panel_.on_seek = [this](int frame) {
        if (session_player_) session_player_->seekToFrame(frame);
    };
    player_panel_.on_speed_changed = [this](float speed) {
        if (session_player_) session_player_->setSpeed(speed);
    };
    player_panel_.on_reference_changed = [this](const std::string& sensor) {
        if (session_player_) session_player_->setReferenceSensor(sensor);
    };
    // Persist the browse folder so next time the dialog reopens at the same place
    player_panel_.on_browse_dir_changed = [this](const std::string& dir) {
        config_.last_session_browse_dir = dir;
        config_.save(FileDialog::getConfigPath());
    };
}

void MainWindow::update() {
    event_bus_.drain();
    handleDevModeShortcut();

    // Poll latest large sensor data (camera + lidar) — not via EventBus
    if (state_.current_mode == AppMode::Logging) {
        for (int i = 0; i < kNumCameras; ++i) {
            std::shared_ptr<CameraFrame> cam;
            if (latest_camera_[i].tryRead(cam) && cam) {
                camera_views_[i].updateFrame(*cam);
            }
        }

        std::shared_ptr<LidarScanData> lidar;
        if (latest_lidar_.tryRead(lidar) && lidar && lidar->num_points > 0) {
            bev_view_.updateLidar(lidar->xyz.data(), lidar->num_points);
        }
    }

    // Update player timer (advance playback if playing)
    if (session_player_ && state_.current_mode == AppMode::Player) {
        session_player_->update(Clock::steady());
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##MainWindow", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    renderModeToggle();
    ImGui::Separator();

    if (state_.current_mode == AppMode::Logging) {
        renderLoggingLayout();
    } else {
        renderPlayerLayout();
    }

    renderStatusBar();
    renderDevModePopup();
    ImGui::End();
}

void MainWindow::handleDevModeShortcut() {
    if (dev_mode_enabled_) return;

    ImGuiIO& io = ImGui::GetIO();
    bool enter_pressed =
        ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (io.KeyCtrl && io.KeyShift && enter_pressed) {
        dev_popup_requested_ = true;
        dev_password_focus_requested_ = true;
        dev_password_buf_[0] = '\0';
        dev_mode_error_.clear();
    }
}

void MainWindow::renderDevModePopup() {
    if (dev_popup_requested_) {
        ImGui::OpenPopup("Developer Mode");
        dev_popup_requested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Developer Mode", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter developer password to enable debug logs.");

        if (dev_password_focus_requested_) {
            ImGui::SetKeyboardFocusHere();
            dev_password_focus_requested_ = false;
        }

        bool submit = ImGui::InputText("Password", dev_password_buf_, sizeof(dev_password_buf_),
                                       ImGuiInputTextFlags_Password |
                                       ImGuiInputTextFlags_EnterReturnsTrue);

        if (!dev_mode_error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", dev_mode_error_.c_str());
        }

        if (submit || ImGui::Button("Unlock", ImVec2(100, 0))) {
            if (std::string(dev_password_buf_) == "devM") {
                dev_mode_enabled_ = true;
                dev_mode_error_.clear();
                dev_password_buf_[0] = '\0';
                msl::log::setConsoleLevel(spdlog::level::debug);
                msl::log::ui()->info("Developer mode enabled (console verbosity = debug; "
                                     "log files are always debug: {})", msl::log::baseDir());
                ImGui::CloseCurrentPopup();
            } else {
                dev_mode_error_ = "Invalid password";
                dev_password_buf_[0] = '\0';
                dev_password_focus_requested_ = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            dev_mode_error_.clear();
            dev_password_buf_[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void MainWindow::renderModeToggle() {
    bool is_logging = (state_.current_mode == AppMode::Logging);
    bool is_player  = (state_.current_mode == AppMode::Player);

    if (is_logging)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.60f, 0.0f, 1.0f));
    if (ImGui::Button("Logging Mode", ImVec2(150, 30))) {
        if (!state_.is_recording) {
            state_.current_mode = AppMode::Logging;
            // Restore live camera if connected
            for (int i = 0; i < kNumCameras; ++i)
                if (!state_.camera_connected[i])
                    camera_views_[i].setDisconnected();
        }
    }
    if (is_logging)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    if (is_player)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.60f, 0.0f, 1.0f));
    if (ImGui::Button("Player Mode", ImVec2(150, 30))) {
        if (!state_.is_recording) {
            state_.current_mode = AppMode::Player;
            // Clear live data from UI when switching to player
            for (auto& v : camera_views_) v.setDisconnected();
        }
    }
    if (is_player)
        ImGui::PopStyleColor();
}

void MainWindow::renderLoggingLayout() {
    settings_panel_.render(config_, state_);
    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        float right_width = ImGui::GetContentRegionAvail().x;
        float top_height = ImGui::GetContentRegionAvail().y * 0.4f;

        renderCameraPanels("CameraPanel", right_width * 0.65f, top_height);

        ImGui::SameLine();

        ImGui::BeginChild("SensorInfoPanel", ImVec2(0, top_height), true);
        sensor_info_view_.render();
        ImGui::EndChild();

        ImGui::BeginChild("BevPanel", ImVec2(0, 0), true);
        bev_view_.render();
        ImGui::EndChild();
    }
    ImGui::EndGroup();
}

void MainWindow::renderCameraPanels(const char* id_prefix, float width, float height) {
    // Show every active channel side by side (one child window each), so two
    // 4K RTSP streams can be monitored — or replayed — at the same time.
    // Logging mode follows the config's "Enable" flags, player mode follows
    // which channels the loaded session actually contains.
    bool active[kNumCameras] = {};
    const bool player_mode = (state_.current_mode == AppMode::Player);
    for (int i = 0; i < kNumCameras; ++i) {
        active[i] = player_mode
                        ? (session_player_ && session_player_->sessionData().has_camera[i])
                        : config_.cameras[i].enabled;
    }

    int shown = 0;
    for (int i = 0; i < kNumCameras; ++i) if (active[i]) ++shown;
    if (shown == 0) shown = 1;   // always keep the layout stable

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float panel_w = (width - spacing * (shown - 1)) / shown;

    int drawn = 0;
    for (int i = 0; i < kNumCameras; ++i) {
        if (!active[i] && !(shown == 1 && i == 0)) continue;
        if (drawn > 0) ImGui::SameLine();

        char child_id[64];
        std::snprintf(child_id, sizeof(child_id), "%s_%d", id_prefix, i);
        ImGui::BeginChild(child_id, ImVec2(panel_w, height), true);
        if (player_mode) {
            ImGui::Text("Camera %d", i + 1);
        } else {
            ImGui::Text("Camera %d%s", i + 1,
                        state_.camera_connected[i].load() ? "" : " (disconnected)");

            // 시간 기준 표시(카메라 이름 오른쪽): 카메라 시각(NTP/RTCP) 매칭인지,
            // 도착 시각(PC 시계) 매칭인지. 연결되어 있을 때만 의미가 있다.
            // NOTE: 기본 ImGui 폰트에는 한글 글리프가 없어 '?' 로 깨진다 → ASCII 만 사용.
            if (state_.camera_connected[i].load()) {
                const char* tag = "[TIME: checking...]";
                char tag_buf[64];
                if (auto* rtsp = dynamic_cast<CameraRtspWorker*>(camera_workers_[i].get())) {
                    switch (rtsp->timeSource()) {
                    case CameraTimeSource::CameraNtp:
                        // 카메라 시각(RTCP/NTP) 기준 매칭 + 측정된 스트림 지연
                        std::snprintf(tag_buf, sizeof(tag_buf), "[NTP MATCH  lat %+.0f ms]",
                                      rtsp->clockDiffSec() * 1000.0);
                        tag = tag_buf;
                        break;
                    case CameraTimeSource::PcClock:
                        tag = "[PC CLOCK MATCH (no NTP)]";
                        break;
                    default:
                        tag = "[TIME: checking...]";
                        break;
                    }
                } else {
                    tag = "[PC CLOCK MATCH]";   // USB 등 RTSP 가 아닌 소스
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%s", tag);
            }
        }
        camera_views_[i].render();
        ImGui::EndChild();
        ++drawn;
    }
}

void MainWindow::renderPlayerLayout() {
    player_panel_.render(config_.data_dir, config_.last_session_browse_dir);
    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        float right_width = ImGui::GetContentRegionAvail().x;
        float top_height = ImGui::GetContentRegionAvail().y * 0.4f;

        renderCameraPanels("CameraPanel_P", right_width * 0.65f, top_height);

        ImGui::SameLine();

        ImGui::BeginChild("SensorInfoPanel_P", ImVec2(0, top_height), true);
        sensor_info_view_.render();
        ImGui::EndChild();

        ImGui::BeginChild("BevPanel_P", ImVec2(0, 0), true);
        bev_view_.render();
        ImGui::EndChild();
    }
    ImGui::EndGroup();
}

void MainWindow::renderStatusBar() {
    if (state_.is_recording) {
        // Left bottom corner (under settings panel, 385px wide)
        ImGui::SetCursorPos(ImVec2(10, ImGui::GetWindowHeight() - 30));
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "  REC");
        ImGui::SameLine();
        double elapsed = Clock::now() - state_.recording_start_time.load();
        ImGui::Text("%s", Clock::formatDuration(elapsed).c_str());
        ImGui::SameLine();
        ImGui::Text("| L:%llu R:%llu C1:%llu C2:%llu G:%llu I:%llu",
                    state_.lidar_frames.load(), state_.radar_frames.load(),
                    state_.camera_frames[0].load(), state_.camera_frames[1].load(),
                    state_.gps_frames.load(), state_.imu_frames.load());
    }
}

// ============================================================================
// Sensor Toggle - actual worker creation/destruction
// ============================================================================

void MainWindow::toggleLidar() {
    if (!ouster_worker_) {
        // Connect
        msl::log::lidar()->info("Lidar: Connecting to {}:{}...", config_.lidar_ip, config_.lidar_port);
        ouster_worker_ = std::make_unique<OusterWorker>(
            event_bus_, config_.lidar_ip, config_.lidar_port, config_.imu_port,
            config_.lidar_multicast_enabled,
            config_.lidar_multicast_dest,
            config_.lidar_mtp_dest,
            config_.lidar_mtp_main);
        ouster_worker_->setLogChannel(msl::log::Channel::Lidar);
        wireOusterCallbacks();
        ouster_worker_->start();
    } else {
        // Disconnect
        ouster_worker_->stop();
        ouster_worker_.reset();
        state_.lidar_connected = false;
        msl::log::lidar()->info("Lidar: Disconnected");
    }
}

void MainWindow::toggleRadar() {
    if (!radar_worker_) {
        msl::log::radar()->info("Radar: Connecting to {}:{}...", config_.radar_ip, config_.radar_port);
        auto radarDataCb = [this](const RadarScanData& scan) {
            if (state_.current_mode == AppMode::Logging) {
                bev_view_.updateRadar(scan.tracks);
            }
            if (state_.is_recording && radar_logger_) {
                radar_logger_->enqueue(scan);
            }
            state_.radar_frames++;
        };

        if (config_.radar_sdk == "BSR30") {
            auto w = std::make_unique<Radar30Worker>(
                event_bus_, config_.radar_ip, config_.radar_port, config_.radar_udp_port);
            w->on_scan_ready = radarDataCb;
            // BSR30 watchdog reconnect — flip a UI state flag so the user
            // sees "Reconnecting..." instead of a stale "Connected".
            w->on_reconnecting = [this](bool reconnecting) {
                state_.radar_reconnecting = reconnecting;
            };
            // BSR30 streaming state — connect and start are now separate
            // user actions. The Start/Stop button label is driven by
            // state_.radar_streaming.
            w->on_streaming_changed = [this](bool streaming) {
                state_.radar_streaming = streaming;
                msl::log::radar()->info("Radar30: streaming={}", streaming);
            };
            radar_worker_ = std::move(w);
        } else {
            auto w = std::make_unique<Radar20Worker>(
                event_bus_, config_.radar_ip, config_.radar_port);
            w->on_scan_ready = radarDataCb;
            radar_worker_ = std::move(w);
        }
        radar_worker_->setLogChannel(msl::log::Channel::Radar);
        radar_worker_->on_connection_changed = [this](bool connected) {
            state_.radar_connected = connected;
            if (connected) msl::log::radar()->info("Radar: Connected");
            else msl::log::radar()->warn("Radar: Connection failed");
        };
        radar_worker_->on_error = [](const std::string& msg) {
            msl::log::radar()->error("Radar: {}", msg);
        };
        radar_worker_->start();
    } else {
        radar_worker_->stop();
        radar_worker_.reset();
        state_.radar_connected = false;
        state_.radar_streaming = false;     // BSR30 cleanup
        state_.radar_reconnecting = false;  // BSR30 cleanup
        msl::log::radar()->info("Radar: Disconnected");
    }
}

void MainWindow::toggleRadarStreaming() {
    // BSR30-only: explicit Start/Stop after Connect. For BSR20 the worker
    // streams as soon as it connects, so this button is hidden in the UI.
    auto* r30 = dynamic_cast<Radar30Worker*>(radar_worker_.get());
    if (!r30) {
        msl::log::radar()->warn("Radar: Start/Stop pressed but no BSR30 worker active");
        return;
    }
    if (state_.radar_streaming.load()) {
        r30->requestStop();
    } else {
        r30->requestStart();
    }
}

void MainWindow::toggleCamera(int idx) {
    if (idx < 0 || idx >= kNumCameras) return;
    auto& cam_cfg = config_.cameras[idx];
    auto& worker  = camera_workers_[idx];

    if (!worker) {
        // Frame callback shared by both worker types: preview + recording.
        // RTSP 는 패스스루 녹화라 이 프레임은 프리뷰 전용이다(축소된 RGB).
        auto onFrame = [this, idx](const CameraFrame& frame) {
            if (state_.current_mode == AppMode::Logging) {
                latest_camera_[idx].write(std::make_shared<CameraFrame>(frame));
            }
            auto& logger = camera_loggers_[idx];
            if (state_.is_recording && logger) {
                if (logger->mode() == CameraLogger::Mode::Passthrough) {
                    // 헤더 기록 전이라면 실제 스트림 해상도를 알려준다.
                    logger->setSourceSize(frame.srcWidth(), frame.srcHeight());
                } else {
                    logger->writeFrame(frame);
                }
            }
            // Update config with actual decoded resolution (원본 해상도 기준)
            if (frame.srcWidth() > 0 && frame.srcHeight() > 0) {
                config_.cameras[idx].width = frame.srcWidth();
                config_.cameras[idx].height = frame.srcHeight();
            }
            state_.camera_frames[idx]++;
        };

        if (cam_cfg.type == "RTSP") {
            msl::log::camera(idx)->info("Camera{}: Connecting RTSP {}...", idx + 1, cam_cfg.rtsp_url);
            auto w = std::make_unique<CameraRtspWorker>(
                event_bus_, cam_cfg.rtsp_url, cam_cfg.width, cam_cfg.height);
            w->on_frame_ready = onFrame;
            // 녹화는 카메라가 보낸 H.264 를 그대로 저장한다(디코딩/인코딩 없음).
            w->on_video_nal = [this, idx](CameraNalPacket&& pkt) {
                if (state_.is_recording && camera_loggers_[idx]) {
                    camera_loggers_[idx]->writeVideoPacket(std::move(pkt));
                }
            };
            w->setFullFrameOutput(false);   // 프리뷰는 축소본으로 충분
            // RTSP 스트림 고정 지연 보정 (config: time_offset_ms)
            w->setTimeOffset(cam_cfg.time_offset_ms / 1000.0);
            // 시간 기준: auto = 카메라 시각 신뢰 가능하면 지연 보정, pc = PC 시계 고정
            w->setPcClockOnly(cam_cfg.time_source == "pc" || cam_cfg.time_source == "PC");
            // Audio packets — passthrough to MOV (mu-law from RTSP).
            w->on_audio_packet = [this, idx](const CameraAudioPacket& pkt) {
                if (state_.is_recording && camera_loggers_[idx]) {
                    camera_loggers_[idx]->writeAudioPacket(pkt);
                }
            };
            worker = std::move(w);
        } else {
            msl::log::camera(idx)->info("Camera{}: Opening USB device {}...", idx + 1, cam_cfg.webcam_index);
            auto w = std::make_unique<CameraUsbWorker>(
                event_bus_, cam_cfg.webcam_index, cam_cfg.width, cam_cfg.height);
            w->on_frame_ready = onFrame;
            worker = std::move(w);
        }
        worker->setLogChannel(idx == 0 ? msl::log::Channel::Camera1
                                       : msl::log::Channel::Camera2);
        worker->on_connection_changed = [this, idx](bool connected) {
            state_.camera_connected[idx] = connected;
            if (connected) {
                msl::log::camera(idx)->info("Camera{}: Connected", idx + 1);
            } else {
                camera_views_[idx].setDisconnected();
            }
        };
        worker->on_error = [idx](const std::string& msg) {
            msl::log::camera(idx)->error("Camera{}: {}", idx + 1, msg);
        };
        worker->start();

        // 녹화 중에 채널을 새로 연결한 경우: 그 채널만 로거를 추가로 열어
        // 남은 구간을 기록한다. PTS 는 녹화 시작 시각 기준이므로 파일 앞부분이
        // 비어 있어도(=늦게 붙어도) 다른 채널/센서와 시간축이 맞는다.
        if (state_.is_recording && !camera_loggers_[idx]) {
            std::string session_name;
            std::string session_dir;
            {
                std::lock_guard<std::mutex> lock(state_.session_mutex);
                session_name = state_.session_name;
                session_dir  = state_.session_dir;
            }
            if (!session_dir.empty()) {
                auto paths = SessionLogger::getFilePaths(session_dir, session_name);
                const bool passthrough =
                    (dynamic_cast<CameraRtspWorker*>(worker.get()) != nullptr);
                camera_loggers_[idx] = std::make_unique<CameraLogger>(
                    paths.cam_mov[idx], cam_cfg.width, cam_cfg.height, cam_cfg.fps,
                    passthrough ? CameraLogger::Mode::Passthrough
                                : CameraLogger::Mode::Encode);
                camera_loggers_[idx]->start();
                worker->setRecording(true, state_.recording_start_time.load());
                if (auto* rtsp = dynamic_cast<CameraRtspWorker*>(worker.get())) {
                    rtsp->resetRecordingClock();
                }
                msl::log::camera(idx)->info("Camera{}: joined the running recording ({})",
                             idx + 1, paths.cam_mov[idx]);
            }
        }
    } else {
        stopCameraWorker(idx);
        camera_views_[idx].setDisconnected();
        msl::log::camera(idx)->info("Camera{}: Disconnected", idx + 1);
    }
}

void MainWindow::stopCameraWorker(int idx) {
    if (idx < 0 || idx >= kNumCameras) return;
    auto& worker = camera_workers_[idx];
    if (!worker) return;

    // RTSP needs a live555 event loop signal before the generic stop().
    if (auto* rtsp = dynamic_cast<CameraRtspWorker*>(worker.get())) {
        rtsp->stopRtsp();
    } else {
        worker->stop();
    }
    worker.reset();
    state_.camera_connected[idx] = false;
}

void MainWindow::toggleGps() {
    if (!gps_worker_) {
        msl::log::gps()->info("GPS: Connecting port='{}' baud={}...", config_.gps_port, config_.gps_baudrate);
        gps_worker_ = std::make_unique<GpsWorker>(
            event_bus_, config_.gps_port, config_.gps_baudrate);
        gps_worker_->setLogChannel(msl::log::Channel::Gps);
        wireGpsCallbacks();
        gps_worker_->start();
    } else {
        gps_worker_->stop();
        gps_worker_.reset();
        state_.gps_connected = false;
        msl::log::gps()->info("GPS: Disconnected");
    }
}

// ============================================================================
// Wire worker callbacks
// ============================================================================

void MainWindow::wireOusterCallbacks() {
    if (!ouster_worker_) return;

    ouster_worker_->on_connection_changed = [this](bool connected) {
        state_.lidar_connected = connected;
        if (connected) msl::log::lidar()->info("Lidar: Connected");
        else msl::log::lidar()->warn("Lidar: Connection failed");
    };
    ouster_worker_->on_error = [](const std::string& msg) {
        msl::log::lidar()->error("Lidar: {}", msg);
    };
    ouster_worker_->on_raw_lidar_packet = [this](const std::vector<uint8_t>& raw, double ts, bool scan_complete) {
        if (state_.is_recording && lidar_logger_) {
            LidarRawPacket pkt;
            pkt.data = raw;
            pkt.pc_ts_rel = ts;
            pkt.is_scan_complete = scan_complete;
            lidar_logger_->enqueue(std::move(pkt));
        }
    };

    ouster_worker_->on_lidar_scan_ready = [this](const LidarScanData& scan) {
        // Write to LatestValue — main thread polls in update()
        if (state_.current_mode == AppMode::Logging && scan.num_points > 0) {
            latest_lidar_.write(std::make_shared<LidarScanData>(scan));
        }
        // Recording is handled by on_raw_lidar_packet above
        state_.lidar_frames++;
    };
    ouster_worker_->on_imu_data_ready = [this](const ImuData& imu) {
        if (state_.current_mode == AppMode::Logging) {
            sensor_info_view_.updateImu(imu);
        }
        if (state_.is_recording && imu_logger_) {
            imu_logger_->enqueue(imu);
        }
        state_.imu_frames++;
    };
}

void MainWindow::wireRadarCallbacks() {
    // Already wired inline in toggleRadar()
}

void MainWindow::wireCameraCallbacks() {
    // Already wired inline in toggleCamera()
}

void MainWindow::wireGpsCallbacks() {
    if (!gps_worker_) return;

    gps_worker_->on_connection_changed = [this](bool connected) {
        state_.gps_connected = connected;
        if (connected) msl::log::gps()->info("GPS: Connected");
        else msl::log::gps()->warn("GPS: Connection failed");
    };
    gps_worker_->on_error = [](const std::string& msg) {
        msl::log::gps()->error("GPS: {}", msg);
    };
    gps_worker_->on_fix_ready = [this](const GpsFix& fix) {
        if (state_.current_mode == AppMode::Logging) {
            sensor_info_view_.updateGps(fix);
        }
        if (state_.is_recording && gps_logger_) {
            gps_logger_->enqueue(std::make_pair(fix.pc_ts_rel, fix));
        }
        state_.gps_frames++;
    };
}

// ============================================================================
// Recording
// ============================================================================

void MainWindow::startRecording() {
    if (!state_.anySensorConnected()) {
        msl::log::recorder()->warn("No sensors connected, cannot start recording");
        return;
    }

    double start_time = Clock::now();
    state_.recording_start_time = start_time;
    state_.resetFrameCounts();

    // Create session directory
    std::string session_name;
    {
        std::lock_guard<std::mutex> lock(state_.session_mutex);
        session_name = state_.session_name;
    }
    std::string session_dir = SessionLogger::createSession(
        config_.data_dir, session_name, config_, start_time);

    {
        std::lock_guard<std::mutex> lock(state_.session_mutex);
        state_.session_dir = session_dir;
    }

    auto paths = SessionLogger::getFilePaths(session_dir, session_name);

    // Create and start loggers for connected sensors
    if (state_.lidar_connected) {
        lidar_logger_ = std::make_unique<LidarLogger>(
            paths.pcap, paths.pcap_idx, config_.lidar_ip, config_.lidar_port);
        lidar_logger_->start();

        // Save sensor metadata for playback
        if (ouster_worker_) {
            std::string meta = ouster_worker_->getMetadataJson();
            if (!meta.empty()) {
                std::ofstream mf(paths.lidar_meta);
                if (mf.is_open()) { mf << meta; mf.close(); }
            }
        }
    }

    if (state_.radar_connected) {
        radar_logger_ = std::make_unique<RadarLogger>(paths.rde, paths.rdei, paths.radar_ini);
        radar_logger_->start();
    }

    // One logger per connected camera channel (Camera/<name>_cam.mov, _cam2.mov)
    for (int i = 0; i < kNumCameras; ++i) {
        if (!state_.camera_connected[i]) continue;
        // RTSP = 패스스루(재인코딩 없음), USB 등 = 기존 인코딩 경로
        const bool passthrough =
            (dynamic_cast<CameraRtspWorker*>(camera_workers_[i].get()) != nullptr);
        camera_loggers_[i] = std::make_unique<CameraLogger>(
            paths.cam_mov[i], config_.cameras[i].width, config_.cameras[i].height,
            config_.cameras[i].fps,
            passthrough ? CameraLogger::Mode::Passthrough : CameraLogger::Mode::Encode);
        camera_loggers_[i]->start();

        // For RTSP: reset PTS origin so the next received frame becomes pc_ts_rel = 0.
        // This anchors the recording's PTS to the camera-side presentationTime
        // (avoids local wall-clock / decode-jitter contamination).
        if (auto* rtsp = dynamic_cast<CameraRtspWorker*>(camera_workers_[i].get())) {
            rtsp->resetRecordingClock();
        }
    }

    if (state_.gps_connected) {
        gps_logger_ = std::make_unique<GpsLogger>(paths.gpsd, paths.gpsdi);
        gps_logger_->start();
    }

    if (state_.lidar_connected) {
        imu_logger_ = std::make_unique<ImuLogger>(paths.imud, paths.imudi);
        imu_logger_->start();
    }

    // Notify workers of recording start
    if (ouster_worker_) ouster_worker_->setRecording(true, start_time);
    if (radar_worker_)  radar_worker_->setRecording(true, start_time);
    for (auto& cw : camera_workers_)
        if (cw) cw->setRecording(true, start_time);
    if (gps_worker_)    gps_worker_->setRecording(true, start_time);

    state_.is_recording = true;
    msl::log::recorder()->info("Recording started: {}", session_dir);
}

void MainWindow::stopRecording() {
    state_.is_recording = false;

    // Notify workers to stop timestamping
    if (ouster_worker_) ouster_worker_->setRecording(false);
    if (radar_worker_)  radar_worker_->setRecording(false);
    for (auto& cw : camera_workers_)
        if (cw) cw->setRecording(false);
    if (gps_worker_)    gps_worker_->setRecording(false);

    // Stop loggers in background thread (flush + file close can be slow)
    std::vector<std::unique_ptr<CameraLogger>> cam_logs;
    for (auto& cl : camera_loggers_)
        if (cl) cam_logs.push_back(std::move(cl));
    auto lid_log  = std::move(lidar_logger_);
    auto rad_log  = std::move(radar_logger_);
    auto gps_log  = std::move(gps_logger_);
    auto imu_log  = std::move(imu_logger_);
    std::string save_dir = state_.session_dir;

    std::thread([cam_logs = std::move(cam_logs),
                 lid_log = std::move(lid_log),
                 rad_log = std::move(rad_log),
                 gps_log = std::move(gps_log),
                 imu_log = std::move(imu_log),
                 save_dir]() mutable {
        msl::log::recorder()->info("Saving session (background)...");
        for (auto& cam_log : cam_logs) {
            if (cam_log) { cam_log->stop(); cam_log.reset(); }
        }
        if (lid_log)  { lid_log->stop();  lid_log.reset(); }
        if (rad_log)  { rad_log->stop();  rad_log.reset(); }
        if (gps_log)  { gps_log->stop();  gps_log.reset(); }
        if (imu_log)  { imu_log->stop();  imu_log.reset(); }
        msl::log::recorder()->info("Recording saved to: {}", save_dir);
    }).detach();

    msl::log::recorder()->info("Recording stopped. Saving in background...");
}

void MainWindow::shutdown() {
    if (state_.is_recording)
        stopRecording();

    // Stop player
    if (session_player_) { session_player_->pause(); session_player_.reset(); }

    // Stop all workers
    if (ouster_worker_) { ouster_worker_->stop(); ouster_worker_.reset(); }
    if (radar_worker_)  { radar_worker_->stop(); radar_worker_.reset(); }
    for (int i = 0; i < kNumCameras; ++i) stopCameraWorker(i);
    if (gps_worker_)    { gps_worker_->stop(); gps_worker_.reset(); }

    bev_view_.destroy();
    config_.save(FileDialog::getConfigPath());
    msl::log::ui()->info("MainWindow shutdown");
}

} // namespace msl
