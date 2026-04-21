#include "main_window.h"
#include "core/clock.h"
#include "core/file_dialog.h"
#include "loggers/session_logger.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <fstream>

namespace msl {

void MainWindow::init() {
    config_.load("config.ini");

    // Set default data directory to exe_dir/Data if not configured
    if (config_.data_dir.empty() || config_.data_dir == "Data") {
        config_.data_dir = FileDialog::getExecutableDir() + "/Data";
    }

    camera_view_.init();
    bev_view_.init();
    sensor_info_view_.setMapCenter(config_.origin_lat, config_.origin_lon);
    connectSettingsCallbacks();
    connectPlayerCallbacks();
    spdlog::info("MainWindow initialized");
}

void MainWindow::connectSettingsCallbacks() {
    settings_panel_.on_lidar_toggle    = [this]() { toggleLidar(); };
    settings_panel_.on_radar_toggle    = [this]() { toggleRadar(); };
    settings_panel_.on_camera_toggle   = [this]() { toggleCamera(); };
    settings_panel_.on_gps_toggle      = [this]() { toggleGps(); };
    settings_panel_.on_start_recording = [this]() { startRecording(); };
    settings_panel_.on_stop_recording  = [this]() { stopRecording(); };

    // RTSP Test: connect briefly, get first frame resolution, disconnect
    settings_panel_.on_rtsp_test = [this](const std::string& url) {
        std::thread([this, url]() {
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
                    event_bus_.post([this, detected_w, detected_h]() {
                        settings_panel_.setRtspTestResult(true, detected_w, detected_h, "");
                        config_.camera_width = detected_w;
                        config_.camera_height = detected_h;
                    });
                } else {
                    event_bus_.post([this]() {
                        settings_panel_.setRtspTestResult(false, 0, 0, "Timeout: no frame received");
                    });
                }
            } catch (const std::exception& e) {
                std::string err = e.what();
                event_bus_.post([this, err]() {
                    settings_panel_.setRtspTestResult(false, 0, 0, err);
                });
            }
        }).detach();
    };
}

void MainWindow::connectPlayerCallbacks() {
    player_panel_.on_load_session = [this](const std::string& path) {
        spdlog::info("Loading session: {}", path);

        session_player_ = std::make_unique<SessionPlayer>(event_bus_);

        // Wire player data callbacks to UI
        session_player_->on_camera_frame = [this](const CameraFrame& f) {
            camera_view_.updateFrame(f);
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
            spdlog::info("Session loaded: {} ({} frames, {:.1f}s)",
                         data.config.session_name,
                         session_player_->totalFrames(),
                         data.total_duration);
        } else {
            spdlog::error("Failed to load session: {}", path);
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
}

void MainWindow::update() {
    event_bus_.drain();

    // Poll latest large sensor data (camera + lidar) — not via EventBus
    if (state_.current_mode == AppMode::Logging) {
        std::shared_ptr<CameraFrame> cam;
        if (latest_camera_.tryRead(cam) && cam) {
            camera_view_.updateFrame(*cam);
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
    ImGui::End();
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
            if (!state_.camera_connected)
                camera_view_.setDisconnected();
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
            camera_view_.setDisconnected();
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

        ImGui::BeginChild("CameraPanel", ImVec2(right_width * 0.65f, top_height), true);
        camera_view_.render();
        ImGui::EndChild();

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

void MainWindow::renderPlayerLayout() {
    player_panel_.render(config_.data_dir);
    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        float right_width = ImGui::GetContentRegionAvail().x;
        float top_height = ImGui::GetContentRegionAvail().y * 0.4f;

        ImGui::BeginChild("CameraPanel_P", ImVec2(right_width * 0.65f, top_height), true);
        camera_view_.render();
        ImGui::EndChild();

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
        ImGui::Text("| L:%llu R:%llu C:%llu G:%llu I:%llu",
                    state_.lidar_frames.load(), state_.radar_frames.load(),
                    state_.camera_frames.load(), state_.gps_frames.load(),
                    state_.imu_frames.load());
    }
}

// ============================================================================
// Sensor Toggle - actual worker creation/destruction
// ============================================================================

void MainWindow::toggleLidar() {
    if (!ouster_worker_) {
        // Connect
        spdlog::info("Lidar: Connecting to {}:{}...", config_.lidar_ip, config_.lidar_port);
        ouster_worker_ = std::make_unique<OusterWorker>(
            event_bus_, config_.lidar_ip, config_.lidar_port, config_.imu_port,
            config_.lidar_multicast_enabled,
            config_.lidar_multicast_dest,
            config_.lidar_mtp_dest,
            config_.lidar_mtp_main);
        wireOusterCallbacks();
        ouster_worker_->start();
    } else {
        // Disconnect
        ouster_worker_->stop();
        ouster_worker_.reset();
        state_.lidar_connected = false;
        spdlog::info("Lidar: Disconnected");
    }
}

void MainWindow::toggleRadar() {
    if (!radar_worker_) {
        spdlog::info("Radar: Connecting to {}:{}...", config_.radar_ip, config_.radar_port);
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
            radar_worker_ = std::move(w);
        } else {
            auto w = std::make_unique<Radar20Worker>(
                event_bus_, config_.radar_ip, config_.radar_port);
            w->on_scan_ready = radarDataCb;
            radar_worker_ = std::move(w);
        }
        radar_worker_->on_connection_changed = [this](bool connected) {
            state_.radar_connected = connected;
            if (connected) spdlog::info("Radar: Connected");
            else spdlog::warn("Radar: Connection failed");
        };
        radar_worker_->on_error = [](const std::string& msg) {
            spdlog::error("Radar: {}", msg);
        };
        radar_worker_->start();
    } else {
        radar_worker_->stop();
        radar_worker_.reset();
        state_.radar_connected = false;
        spdlog::info("Radar: Disconnected");
    }
}

void MainWindow::toggleCamera() {
    if (!camera_worker_) {
        if (config_.camera_type == "RTSP") {
            spdlog::info("Camera: Connecting RTSP {}...", config_.camera_rtsp_url);
            auto w = std::make_unique<CameraRtspWorker>(
                event_bus_, config_.camera_rtsp_url, config_.camera_width, config_.camera_height);
            w->on_frame_ready = [this](const CameraFrame& frame) {
                if (state_.current_mode == AppMode::Logging) {
                    latest_camera_.write(std::make_shared<CameraFrame>(frame));
                }
                if (state_.is_recording && camera_logger_) {
                    camera_logger_->writeFrame(frame);
                }
                // Update config with actual decoded resolution
                if (frame.width > 0 && frame.height > 0) {
                    config_.camera_width = frame.width;
                    config_.camera_height = frame.height;
                }
                state_.camera_frames++;
            };
            camera_worker_ = std::move(w);
        } else {
            spdlog::info("Camera: Opening USB device {}...", config_.camera_webcam_index);
            auto w = std::make_unique<CameraUsbWorker>(
                event_bus_, config_.camera_webcam_index, config_.camera_width, config_.camera_height);
            w->on_frame_ready = [this](const CameraFrame& frame) {
                if (state_.current_mode == AppMode::Logging) {
                    latest_camera_.write(std::make_shared<CameraFrame>(frame));
                }
                if (state_.is_recording && camera_logger_) {
                    camera_logger_->writeFrame(frame);
                }
                if (frame.width > 0 && frame.height > 0) {
                    config_.camera_width = frame.width;
                    config_.camera_height = frame.height;
                }
                state_.camera_frames++;
            };
            camera_worker_ = std::move(w);
        }
        camera_worker_->on_connection_changed = [this](bool connected) {
            state_.camera_connected = connected;
            if (connected) {
                spdlog::info("Camera: Connected");
            } else {
                camera_view_.setDisconnected();
            }
        };
        camera_worker_->on_error = [](const std::string& msg) {
            spdlog::error("Camera: {}", msg);
        };
        camera_worker_->start();
    } else {
        // RTSP needs special stop to signal live555 event loop first
        if (config_.camera_type == "RTSP") {
            auto* rtsp = dynamic_cast<CameraRtspWorker*>(camera_worker_.get());
            if (rtsp) rtsp->stopRtsp();
        } else {
            camera_worker_->stop();
        }
        camera_worker_.reset();
        camera_view_.setDisconnected();
        state_.camera_connected = false;
        spdlog::info("Camera: Disconnected");
    }
}

void MainWindow::toggleGps() {
    if (!gps_worker_) {
        spdlog::info("GPS: Connecting port='{}' baud={}...", config_.gps_port, config_.gps_baudrate);
        gps_worker_ = std::make_unique<GpsWorker>(
            event_bus_, config_.gps_port, config_.gps_baudrate);
        wireGpsCallbacks();
        gps_worker_->start();
    } else {
        gps_worker_->stop();
        gps_worker_.reset();
        state_.gps_connected = false;
        spdlog::info("GPS: Disconnected");
    }
}

// ============================================================================
// Wire worker callbacks
// ============================================================================

void MainWindow::wireOusterCallbacks() {
    if (!ouster_worker_) return;

    ouster_worker_->on_connection_changed = [this](bool connected) {
        state_.lidar_connected = connected;
        if (connected) spdlog::info("Lidar: Connected");
        else spdlog::warn("Lidar: Connection failed");
    };
    ouster_worker_->on_error = [](const std::string& msg) {
        spdlog::error("Lidar: {}", msg);
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
        if (connected) spdlog::info("GPS: Connected");
        else spdlog::warn("GPS: Connection failed");
    };
    gps_worker_->on_error = [](const std::string& msg) {
        spdlog::error("GPS: {}", msg);
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
        spdlog::warn("No sensors connected, cannot start recording");
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

    if (state_.camera_connected) {
        camera_logger_ = std::make_unique<CameraLogger>(
            paths.mp4, config_.camera_width, config_.camera_height, config_.camera_fps);
        camera_logger_->start();
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
    if (camera_worker_) camera_worker_->setRecording(true, start_time);
    if (gps_worker_)    gps_worker_->setRecording(true, start_time);

    state_.is_recording = true;
    spdlog::info("Recording started: {}", session_dir);
}

void MainWindow::stopRecording() {
    state_.is_recording = false;

    // Notify workers to stop timestamping
    if (ouster_worker_) ouster_worker_->setRecording(false);
    if (radar_worker_)  radar_worker_->setRecording(false);
    if (camera_worker_) camera_worker_->setRecording(false);
    if (gps_worker_)    gps_worker_->setRecording(false);

    // Stop loggers in background thread (flush + file close can be slow)
    auto cam_log  = std::move(camera_logger_);
    auto lid_log  = std::move(lidar_logger_);
    auto rad_log  = std::move(radar_logger_);
    auto gps_log  = std::move(gps_logger_);
    auto imu_log  = std::move(imu_logger_);
    std::string save_dir = state_.session_dir;

    std::thread([cam_log = std::move(cam_log),
                 lid_log = std::move(lid_log),
                 rad_log = std::move(rad_log),
                 gps_log = std::move(gps_log),
                 imu_log = std::move(imu_log),
                 save_dir]() mutable {
        spdlog::info("Saving session (background)...");
        if (cam_log)  { cam_log->stop();  cam_log.reset(); }
        if (lid_log)  { lid_log->stop();  lid_log.reset(); }
        if (rad_log)  { rad_log->stop();  rad_log.reset(); }
        if (gps_log)  { gps_log->stop();  gps_log.reset(); }
        if (imu_log)  { imu_log->stop();  imu_log.reset(); }
        spdlog::info("Recording saved to: {}", save_dir);
    }).detach();

    spdlog::info("Recording stopped. Saving in background...");
}

void MainWindow::shutdown() {
    if (state_.is_recording)
        stopRecording();

    // Stop player
    if (session_player_) { session_player_->pause(); session_player_.reset(); }

    // Stop all workers
    if (ouster_worker_) { ouster_worker_->stop(); ouster_worker_.reset(); }
    if (radar_worker_)  { radar_worker_->stop(); radar_worker_.reset(); }
    if (camera_worker_) {
        auto* rtsp = dynamic_cast<CameraRtspWorker*>(camera_worker_.get());
        if (rtsp) rtsp->stopRtsp(); else camera_worker_->stop();
        camera_worker_.reset();
    }
    if (gps_worker_)    { gps_worker_->stop(); gps_worker_.reset(); }

    bev_view_.destroy();
    config_.save("config.ini");
    spdlog::info("MainWindow shutdown");
}

} // namespace msl
