/*=============================================================================
 * BSR20 Radar Viewer — Dear ImGui + OpenGL Sample
 *
 * Visualizes BSR20 radar tracks with a 2D top-view canvas,
 * lane configuration, and detect line configuration panels.
 *
 * Dependencies: GLFW, Dear ImGui, OpenGL, BSR20_SDK
 *=============================================================================*/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#endif

#include "Bsr20Sdk.h"

#ifdef HAS_SPDLOG
#include "logger.h"
#else
// Fallback: simple message logging when spdlog is not available
#include <cstdio>
static inline void _log_msg(const char* level, const char* msg) {
    printf("[%s] %s\n", level, msg); fflush(stdout);
}
#define LOG_INFO(msg, ...)  _log_msg("INFO", msg)
#define LOG_WARN(msg, ...)  _log_msg("WARN", msg)
#define LOG_ERROR(msg, ...) _log_msg("ERROR", msg)
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// Constants
// ============================================================
static constexpr int   MAX_LANES       = 16;
static constexpr int   MAX_DET_LINES   = 5;

// Track type colors (ABGR for ImGui)
static const ImU32 kTrackColors[] = {
    IM_COL32(255, 255,   0, 255),  // 0: yellow
    IM_COL32(  0, 200, 255, 255),  // 1: cyan
    IM_COL32(  0, 255,   0, 255),  // 2: green
    IM_COL32(255, 128,   0, 255),  // 3: orange
    IM_COL32(255,   0, 255, 255),  // 4: magenta
    IM_COL32(128, 128, 255, 255),  // 5: light blue
    IM_COL32(255, 255, 255, 255),  // 6: white
    IM_COL32(200, 200, 200, 255),  // 7: gray
};

// Detect line colors (one per line index)
static const ImU32 kDetLineColors[] = {
    IM_COL32(255,  80,  80, 220),  // 0: red
    IM_COL32( 80, 220,  80, 220),  // 1: green
    IM_COL32( 80, 120, 255, 220),  // 2: blue
    IM_COL32(255, 180,  40, 220),  // 3: orange
    IM_COL32(200,  80, 220, 220),  // 4: purple
};

// ============================================================
// VDS Event (viewer-local, not SDK)
// ============================================================
static constexpr int   MAX_VDS_EVENTS  = 100;
static constexpr float MIN_SPEED_KPH   = 2.0f;
static constexpr int   MAX_PASS_TRK_ID = 256;

struct VdsEvent {
    int      laneNum;
    uint16_t trackId;
    float    speed;    // kph
    float    yPos;     // meters
    uint32_t scanIdx;
};

// ============================================================
// Shared data between receive thread and render thread
// ============================================================
struct SharedRadarData {
    std::mutex                 mtx;
    BydaScanInfo               scanInfo{};
    std::vector<BydaTrack>     tracks;
    bool                       hasNewData  = false;
    bool                       connected   = false;
    int                        paramStep   = -1;  // -1=not started

    // Loaded parameters
    BydaInstallInfo            installInfo{};
    bool                       hasInstall  = false;
    BydaLaneInfo               lanes[MAX_LANES]{};
    int                        laneCount   = 0;
    bool                       laneEditorReady = false;

    // Detect lines
    BydaDetectLineInfo         detectLines[MAX_DET_LINES]{};
    int                        detectLineCount = 0;
    bool                       hasDetectLines  = false;
    bool                       detectLineEditorReady = false;
};

// ============================================================
// Application state
// ============================================================
enum class AppState {
    Disconnected,
    Connecting,
    LoadingParams,
    Streaming,
};

// ============================================================
// Radar Viewer Application
// ============================================================
class RadarViewerApp {
public:
    RadarViewerApp() {
        std::strncpy(ipBuf_, "127.0.0.1", sizeof(ipBuf_));
    }

    ~RadarViewerApp() { shutdown(); }

    int run() {
        if (!initWindow()) return -1;

        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Pull data from receive thread
            pullRadarData();

            // Main UI
            renderUI();

            // Render
            ImGui::Render();
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window_);
        }

        shutdown();
        return 0;
    }

private:
    // --------------------------------------------------------
    // Window / ImGui init
    // --------------------------------------------------------
    bool initWindow() {
        if (!glfwInit()) return false;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window_ = glfwCreateWindow(1400, 800, "BSR20 Radar Viewer", nullptr, nullptr);
        if (!window_) { glfwTerminate(); return false; }

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);  // vsync

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        return true;
    }

    void shutdown() {
        stopReceiveThread();

        if (window_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(window_);
            glfwTerminate();
            window_ = nullptr;
        }
    }

    // --------------------------------------------------------
    // Receive thread
    // --------------------------------------------------------
    void startReceiveThread() {
        if (recvThread_.joinable()) return;
        recvRunning_ = true;
        recvThread_  = std::thread(&RadarViewerApp::receiveLoop, this);
    }

    void stopReceiveThread() {
        recvRunning_ = false;
        if (recvThread_.joinable())
            recvThread_.join();

        if (handle_) {
            byda_disconnect(handle_);
            byda_destroy(handle_);
            handle_ = nullptr;
        }
        state_ = AppState::Disconnected;

        std::lock_guard<std::mutex> lk(shared_.mtx);
        shared_.connected  = false;
        shared_.hasNewData = false;
        shared_.paramStep  = -1;
        shared_.hasInstall = false;
        shared_.hasDetectLines = false;
        shared_.laneCount  = 0;
        shared_.detectLineCount = 0;

        // Reset VDS state
        vdsEvents_.clear();
        std::memset(passTrkList_, 0, sizeof(passTrkList_));
        std::memset(lastVdsScan_, 0, sizeof(lastVdsScan_));
        std::memset(vdsHighlightTracks_, 0, sizeof(vdsHighlightTracks_));
    }

    void receiveLoop() {
        int cycle = 0;
        int paramStep = 0;  // 0=load install, 1=req lane, 2=wait lane, 3=req detect, 4=wait detect, 5=done

        // Start parameter loading (background, won't block streaming)
        byda_load_install(handle_);
        state_ = AppState::Streaming;
        LOG_INFO("Streaming started");

        while (recvRunning_) {
            int bytes = byda_receive(handle_);
            if (bytes <= 0) {
                if (bytes < 0) {
                    std::lock_guard<std::mutex> lk(shared_.mtx);
                    shared_.connected = false;
                    break;
                }
                continue;
            }

            int trackCount = byda_process(handle_);
            cycle++;

            // Apply pending lane save from UI
            if (saveLanes_.load()) {
                BydaLaneInfo tmpLanes[16]{};
                int tmpCount = 0;
                {
                    std::lock_guard<std::mutex> lk(laneSaveMtx_);
                    tmpCount = pendingLaneCount_;
                    for (int i = 0; i < tmpCount; i++)
                        tmpLanes[i] = pendingLanes_[i];
                }
                int maxL = byda_get_max_lanes(handle_);
                // Clear all lanes first
                for (int i = 0; i < maxL; i++) {
                    BydaLaneInfo off{};
                    byda_set_lane(handle_, i, &off);
                }
                // Set configured lanes
                for (int i = 0; i < tmpCount && i < maxL; i++) {
                    byda_set_lane(handle_, i, &tmpLanes[i]);
                }
                int rc = byda_save_lanes(handle_);
                int lc = byda_get_lane_count(handle_);
                LOG_INFO("Lanes saved: {} lanes (rc={})", lc, rc);
                fflush(stdout);

                // Refresh lane data in shared
                {
                    std::lock_guard<std::mutex> lk(shared_.mtx);
                    shared_.laneCount = (lc > MAX_LANES) ? MAX_LANES : lc;
                    for (int i = 0; i < shared_.laneCount; i++)
                        byda_get_lane_info(handle_, i, &shared_.lanes[i]);
                    shared_.laneEditorReady = true;
                }
                saveLanes_ = false;
            }

            // Apply pending detect line save from UI
            if (saveDetLines_.load()) {
                BydaDetectLineInfo tmpLines[MAX_DET_LINES]{};
                int tmpCount = 0;
                {
                    std::lock_guard<std::mutex> lk(detLineSaveMtx_);
                    tmpCount = pendingDetLineCount_;
                    for (int i = 0; i < tmpCount; i++)
                        tmpLines[i] = pendingDetLines_[i];
                }
                for (int i = 0; i < tmpCount; i++) {
                    byda_set_detect_line_info(handle_, i, &tmpLines[i]);
                }
                LOG_INFO("Detect lines saved: {} lines", tmpCount);
                fflush(stdout);

                // Refresh detect line data in shared
                {
                    std::lock_guard<std::mutex> lk(shared_.mtx);
                    shared_.detectLineCount = tmpCount;
                    for (int i = 0; i < tmpCount; i++)
                        byda_get_detect_line_info(handle_, i, &shared_.detectLines[i]);
                    shared_.detectLineEditorReady = true;
                }
                saveDetLines_ = false;
            }

            // Parameter loading with timeout-based forced reset
            if (paramStep < 6) {
                switch (paramStep) {
                case 0: // wait for install response
                    if (cycle == 10) {
                        if (byda_is_param_idle(handle_)) {
                            BydaInstallInfo info{};
                            if (byda_get_install_info(handle_, &info) == BYDA_OK) {
                                std::lock_guard<std::mutex> lk(shared_.mtx);
                                shared_.installInfo = info;
                                shared_.hasInstall  = true;
                            }
                        } else {
                            byda_reset_param_state(handle_);
                        }
                        paramStep = 1;
                    }
                    break;
                case 1: // request lane
                    if (cycle == 12) {
                        byda_load_lane(handle_);
                        paramStep = 2;
                    }
                    break;
                case 2: // wait for lane response
                    if (cycle == 25) {
                        if (byda_is_param_idle(handle_)) {
                            int lc = byda_get_lane_count(handle_);
                            {
                                std::lock_guard<std::mutex> lk(shared_.mtx);
                                shared_.laneCount = (lc > MAX_LANES) ? MAX_LANES : lc;
                                for (int i = 0; i < shared_.laneCount; i++)
                                    byda_get_lane_info(handle_, i, &shared_.lanes[i]);
                                shared_.laneEditorReady = true;
                            }
                            LOG_INFO("Loaded {} lanes from radar", lc);
                        } else {
                            byda_reset_param_state(handle_);
                        }
                        paramStep = 3;
                    }
                    break;
                case 3: // request detect lines
                    if (cycle == 27) {
                        byda_load_detect(handle_);
                        paramStep = 4;
                    }
                    break;
                case 4: // wait for detect line response
                    if (cycle == 40) {
                        if (byda_is_param_idle(handle_)) {
                            int dlCount = byda_get_detect_line_count(handle_);
                            if (dlCount > MAX_DET_LINES) dlCount = MAX_DET_LINES;
                            {
                                std::lock_guard<std::mutex> lk(shared_.mtx);
                                shared_.detectLineCount = dlCount;
                                for (int i = 0; i < dlCount; i++)
                                    byda_get_detect_line_info(handle_, i, &shared_.detectLines[i]);
                                shared_.hasDetectLines = true;
                                shared_.detectLineEditorReady = true;
                            }
                            LOG_INFO("Loaded {} detect lines from radar", dlCount);
                        } else {
                            byda_reset_param_state(handle_);
                        }
                        paramStep = 5;
                    }
                    break;
                case 5: // done
                    LOG_INFO("Param loading complete (commVer={})",
                             byda_get_comm_version(handle_));
                    fflush(stdout);
                    paramStep = 6;
                    break;
                }
            }

            // Copy track data
            {
                std::lock_guard<std::mutex> lk(shared_.mtx);
                byda_get_scan_info(handle_, &shared_.scanInfo);

                shared_.tracks.resize(trackCount > 0 ? trackCount : 0);
                for (int i = 0; i < trackCount; i++)
                    byda_get_track(handle_, i, &shared_.tracks[i]);

                shared_.hasNewData = true;
            }
        }

        recvRunning_ = false;
    }

    // --------------------------------------------------------
    // Pull data from shared buffer (called on main thread)
    // --------------------------------------------------------
    void pullRadarData() {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        if (!shared_.hasNewData) return;

        localScan_   = shared_.scanInfo;
        localTracks_ = shared_.tracks;

        shared_.hasNewData = false;

        // Sync lane data to editor when newly loaded from radar
        if (shared_.laneEditorReady) {
            shared_.laneEditorReady = false;
            editLaneCount_ = shared_.laneCount;
            for (int i = 0; i < shared_.laneCount; i++)
                editLanes_[i] = shared_.lanes[i];
            lanesLoaded_ = true;
        }

        // Sync detect line data to editor when newly loaded from radar
        if (shared_.detectLineEditorReady) {
            shared_.detectLineEditorReady = false;
            editDetLineCount_ = shared_.detectLineCount;
            for (int i = 0; i < shared_.detectLineCount; i++)
                editDetLines_[i] = shared_.detectLines[i];
            detLinesLoaded_ = true;
        }

        // ---- VDS Detection Engine ----
        if (vdsEnabled_) {
            runVdsDetection();
        }
    }

    // --------------------------------------------------------
    // VDS Detection Engine (main thread)
    // --------------------------------------------------------
    void runVdsDetection() {
        uint32_t curScan = localScan_.scan_index;

        // For each track
        for (auto& t : localTracks_) {
            // Speed in kph: velocity is in m/s-like units from radar
            // y_vel is the longitudinal velocity; compute speed magnitude
            float speedKph = std::fabs(t.y_vel) * 3.6f;
            if (speedKph < MIN_SPEED_KPH) continue;

            int trkIdx = t.id & 0xFF;  // clamp to 0-255

            // For each enabled detect line
            for (int dli = 0; dli < shared_.detectLineCount; dli++) {
                auto& dl = shared_.detectLines[dli];
                if (!dl.enable) continue;

                // For each enabled lane
                for (int li = 0; li < shared_.laneCount; li++) {
                    auto& ln = shared_.lanes[li];
                    if (!ln.enable) continue;

                    // Check if track is within lane x-range
                    float laneXMin = ln.start_pos;
                    float laneXMax = ln.start_pos + ln.width;
                    if (t.x_pos < laneXMin || t.x_pos > laneXMax) continue;

                    // Determine band based on lane direction
                    float bandSt, bandEnd;
                    bool dirMatch = false;

                    if (ln.direction == 0) {
                        // UP: velocity >= 0, band = [pos, pos + bandWidth]
                        if (t.y_vel >= 0) {
                            bandSt  = dl.position;
                            bandEnd = dl.position + vdsBandWidth_;
                            dirMatch = (t.y_pos >= bandSt && t.y_pos <= bandEnd);
                        }
                    } else if (ln.direction == 1) {
                        // DOWN: velocity < 0, band = [pos - bandWidth, pos]
                        if (t.y_vel < 0) {
                            bandSt  = dl.position - vdsBandWidth_;
                            bandEnd = dl.position;
                            dirMatch = (t.y_pos >= bandSt && t.y_pos <= bandEnd);
                        }
                    } else {
                        // UpDown: check both directions
                        if (t.y_vel >= 0) {
                            bandSt  = dl.position;
                            bandEnd = dl.position + vdsBandWidth_;
                            dirMatch = (t.y_pos >= bandSt && t.y_pos <= bandEnd);
                        } else {
                            bandSt  = dl.position - vdsBandWidth_;
                            bandEnd = dl.position;
                            dirMatch = (t.y_pos >= bandSt && t.y_pos <= bandEnd);
                        }
                    }

                    if (!dirMatch) continue;

                    // Check if already counted (pass track list)
                    if (passTrkList_[dli][trkIdx]) continue;

                    // Car2car time filtering
                    int lastScan = lastVdsScan_[dli][li];
                    if (lastScan > 0) {
                        float timeDiff = (float)(curScan - (uint32_t)lastScan) * 0.035f;
                        if (timeDiff < vdsCar2CarTime_) continue;
                    }

                    // Record VDS event
                    VdsEvent ev;
                    ev.laneNum = ln.real_lane_num;
                    ev.trackId = t.id;
                    ev.speed   = speedKph;
                    ev.yPos    = t.y_pos;
                    ev.scanIdx = curScan;

                    vdsEvents_.push_front(ev);
                    if ((int)vdsEvents_.size() > MAX_VDS_EVENTS)
                        vdsEvents_.pop_back();

                    // Mark track as counted
                    passTrkList_[dli][trkIdx] = true;
                    lastVdsScan_[dli][li] = (int)curScan;

                    // Record track ID for pulsing highlight
                    vdsHighlightTracks_[trkIdx] = curScan;
                }
            }

            // Clear pass track list for tracks that have LEFT the band
            for (int dli = 0; dli < shared_.detectLineCount; dli++) {
                auto& dl = shared_.detectLines[dli];
                if (!dl.enable) continue;

                // Check if track is outside ALL bands for this detect line
                float bandMin = dl.position - vdsBandWidth_;
                float bandMax = dl.position + vdsBandWidth_;
                if (t.y_pos < bandMin || t.y_pos > bandMax) {
                    passTrkList_[dli][trkIdx] = false;
                }
            }
        }
    }

    // --------------------------------------------------------
    // Main UI
    // --------------------------------------------------------
    void renderUI() {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##Main", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        float totalW = ImGui::GetContentRegionAvail().x;
        float panelW = 380.0f;
        float canvasW = totalW - panelW - 8.0f;

        // Left: 2D Canvas
        ImGui::BeginChild("Canvas", ImVec2(canvasW, 0), true);
        render2DView();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right: Controls
        ImGui::BeginChild("Panel", ImVec2(panelW, 0), true);
        renderPanel();
        ImGui::EndChild();

        ImGui::End();
    }

    // --------------------------------------------------------
    // Right panel
    // --------------------------------------------------------
    void renderPanel() {
        // -- Connection --
        if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("IP", ipBuf_, sizeof(ipBuf_));
            ImGui::InputInt("Port", &port_);

            if (state_ == AppState::Disconnected) {
                if (ImGui::Button("Connect", ImVec2(-1, 0))) {
                    doConnect();
                }
            } else {
                if (ImGui::Button("Disconnect", ImVec2(-1, 0))) {
                    stopReceiveThread();
                }
            }

            // Status
            const char* stateStr = "Disconnected";
            ImVec4 stateColor = ImVec4(1, 0.3f, 0.3f, 1);
            switch (state_) {
            case AppState::Connecting:    stateStr = "Connecting...";    stateColor = ImVec4(1,1,0,1); break;
            case AppState::LoadingParams: stateStr = "Loading Params..."; stateColor = ImVec4(1,1,0,1); break;
            case AppState::Streaming:     stateStr = "Streaming";        stateColor = ImVec4(0,1,0,1); break;
            default: break;
            }
            ImGui::TextColored(stateColor, "Status: %s", stateStr);

            if (state_ == AppState::Streaming) {
                ImGui::Text("Scan #%u  Tracks: %d",
                    localScan_.scan_index, (int)localTracks_.size());
            }
            ImGui::Separator();
        }

        // -- Install Info --
        {
            std::lock_guard<std::mutex> lk(shared_.mtx);
            if (shared_.hasInstall && ImGui::CollapsingHeader("Install Info")) {
                auto& i = shared_.installInfo;
                ImGui::Text("Height: %.1f m", i.height);
                ImGui::Text("Distance: %.1f m", i.distance);
                ImGui::Text("Pitch: %.1f deg", i.pitch);
                ImGui::Text("Azimuth: %.1f deg", i.azimuth);
                ImGui::Text("Direction: %s", i.direction == 0 ? "Up" : "Down");
                ImGui::Separator();
            }
        }

        // -- Detect Line Configuration --
        if (ImGui::CollapsingHeader("Detect Line Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Detect Lines: %d", editDetLineCount_);

            for (int i = 0; i < editDetLineCount_; i++) {
                ImGui::PushID(1000 + i);
                auto& dl = editDetLines_[i];

                char hdr[48];
                snprintf(hdr, sizeof(hdr), "Detect Line %d##DL%d", i + 1, i);

                if (ImGui::TreeNode(hdr)) {
                    bool en = dl.enable != 0;
                    ImGui::Checkbox("Enable", &en);
                    dl.enable = en ? 1 : 0;

                    ImGui::DragFloat("Position (m)", &dl.position, 0.5f, 0.0f, 300.0f);

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (state_ == AppState::Streaming && editDetLineCount_ > 0) {
                if (ImGui::Button("Save Detect Lines to Radar", ImVec2(-1, 0))) {
                    std::lock_guard<std::mutex> lk(detLineSaveMtx_);
                    pendingDetLineCount_ = editDetLineCount_;
                    for (int i = 0; i < editDetLineCount_; i++)
                        pendingDetLines_[i] = editDetLines_[i];
                    saveDetLines_ = true;
                    detLinesLoaded_ = false;  // Force re-sync after save
                }
            }
            ImGui::Separator();
        }

        // -- Lane Configuration --
        if (ImGui::CollapsingHeader("Lane Configuration")) {
            const char* dirNames[] = { "Up", "Down", "UpDown" };
            const char* typeNames[] = { "Normal", "Bus", "Side" };

            ImGui::Text("Lanes: %d / %d", editLaneCount_, maxLanes_);
            ImGui::SameLine();
            if (editLaneCount_ < maxLanes_ && ImGui::SmallButton("+Add")) {
                auto& nl = editLanes_[editLaneCount_];
                nl.enable = 1;
                nl.type = 0;
                nl.direction = 1;  // Down
                nl.real_lane_num = (uint8_t)(editLaneCount_ + 1);
                nl.start_pos = -5.0f + editLaneCount_ * 3.5f;
                nl.width = 3.5f;
                editLaneCount_++;
            }

            for (int i = 0; i < editLaneCount_; i++) {
                ImGui::PushID(i);
                auto& ln = editLanes_[i];

                bool en = ln.enable != 0;
                char hdr[32];
                snprintf(hdr, sizeof(hdr), "Lane %d##L%d", i + 1, i);

                if (ImGui::TreeNode(hdr)) {
                    ImGui::Checkbox("Enable", &en);
                    ln.enable = en ? 1 : 0;

                    int dir = ln.direction;
                    ImGui::Combo("Direction", &dir, dirNames, 3);
                    ln.direction = (uint8_t)dir;

                    int typ = ln.type;
                    ImGui::Combo("Type", &typ, typeNames, 3);
                    ln.type = (uint8_t)typ;

                    int rln = ln.real_lane_num;
                    ImGui::InputInt("Lane#", &rln);
                    if (rln < 0) rln = 0; if (rln > 31) rln = 31;
                    ln.real_lane_num = (uint8_t)rln;

                    ImGui::DragFloat("Start X (m)", &ln.start_pos, 0.1f, -50.0f, 50.0f);
                    ImGui::DragFloat("Width (m)",   &ln.width,     0.1f, 0.5f, 30.0f);

                    // Delete button
                    if (ImGui::SmallButton("Delete")) {
                        for (int j = i; j < editLaneCount_ - 1; j++)
                            editLanes_[j] = editLanes_[j + 1];
                        editLaneCount_--;
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (state_ == AppState::Streaming && editLaneCount_ > 0) {
                if (ImGui::Button("Save Lanes to Radar", ImVec2(-1, 0))) {
                    std::lock_guard<std::mutex> lk(laneSaveMtx_);
                    pendingLaneCount_ = editLaneCount_;
                    for (int i = 0; i < editLaneCount_; i++)
                        pendingLanes_[i] = editLanes_[i];
                    saveLanes_ = true;
                    lanesLoaded_ = false;  // Force re-sync after save
                }
            }
            ImGui::Separator();
        }

        // -- VDS Configuration (LOCAL, not saved to radar) --
        if (ImGui::CollapsingHeader("VDS Configuration (Local)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable VDS Detection", &vdsEnabled_);
            ImGui::DragFloat("Band Width (m)", &vdsBandWidth_, 0.1f, 0.5f, 20.0f, "%.1f");
            ImGui::DragFloat("Car2Car Time (s)", &vdsCar2CarTime_, 0.05f, 0.1f, 5.0f, "%.2f");

            if (ImGui::Button("Clear VDS Events", ImVec2(-1, 0))) {
                vdsEvents_.clear();
                std::memset(passTrkList_, 0, sizeof(passTrkList_));
                std::memset(lastVdsScan_, 0, sizeof(lastVdsScan_));
                std::memset(vdsHighlightTracks_, 0, sizeof(vdsHighlightTracks_));
            }

            ImGui::Text("Events: %d", (int)vdsEvents_.size());
            ImGui::Separator();
        }

        // -- VDS Event Log --
        if (ImGui::CollapsingHeader("VDS Event Log", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("VdsLog", ImVec2(0, 200), true,
                ImGuiWindowFlags_HorizontalScrollbar);

            // Header
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f),
                "%-8s %-6s %-6s %-10s %-10s", "Scan#", "Lane", "TrkID", "Speed", "Y-Pos");
            ImGui::Separator();

            for (auto& ev : vdsEvents_) {
                ImGui::Text("%-8u %-6d %-6d %-10.1f %-10.1f",
                    ev.scanIdx, ev.laneNum, ev.trackId, ev.speed, ev.yPos);
            }

            ImGui::EndChild();
            ImGui::Separator();
        }
    }

    // --------------------------------------------------------
    // 2D top-view canvas
    // --------------------------------------------------------
    void render2DView() {
        ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 50 || canvasSize.y < 50) return;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
            IM_COL32(20, 25, 30, 255));

        // Handle mouse pan/zoom
        ImGui::InvisibleButton("canvas_input", canvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        bool hovered = ImGui::IsItemHovered();

        if (hovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0) {
                float factor = (wheel > 0) ? 1.15f : (1.0f / 1.15f);
                // Zoom towards mouse
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                float mx = mousePos.x - canvasPos.x - canvasSize.x * 0.5f - panX_;
                float my = mousePos.y - canvasPos.y - canvasSize.y * 0.85f - panY_;
                panX_ += mx * (1.0f - factor);
                panY_ += my * (1.0f - factor);
                scale_ *= factor;
                if (scale_ < 0.5f) scale_ = 0.5f;
                if (scale_ > 50.0f) scale_ = 50.0f;
            }
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            panX_ += ImGui::GetIO().MouseDelta.x;
            panY_ += ImGui::GetIO().MouseDelta.y;
        }
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            panX_ += ImGui::GetIO().MouseDelta.x;
            panY_ += ImGui::GetIO().MouseDelta.y;
        }

        // Coordinate transform: world (meters) -> screen pixels
        // Radar at bottom-center, Y increases upward
        auto w2s = [&](float wx, float wy) -> ImVec2 {
            float sx = canvasPos.x + canvasSize.x * 0.5f + wx * scale_ + panX_;
            float sy = canvasPos.y + canvasSize.y * 0.85f - wy * scale_ + panY_;
            return ImVec2(sx, sy);
        };

        // Clip
        dl->PushClipRect(canvasPos,
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        // Grid lines
        drawGrid(dl, canvasPos, canvasSize, w2s);

        // Draw lanes
        drawLanes(dl, w2s);

        // Draw detect lines
        drawDetectLines(dl, canvasPos, canvasSize, w2s);

        // Draw radar position marker
        {
            ImVec2 rp = w2s(0, 0);
            dl->AddTriangleFilled(
                ImVec2(rp.x, rp.y - 8),
                ImVec2(rp.x - 6, rp.y + 4),
                ImVec2(rp.x + 6, rp.y + 4),
                IM_COL32(255, 255, 255, 255));
            dl->AddText(ImVec2(rp.x + 10, rp.y - 6), IM_COL32(255,255,255,200), "Radar");
        }

        // Draw tracks
        drawTracks(dl, w2s);

        dl->PopClipRect();

        // Legend overlay
        drawLegend(dl, canvasPos);

        // Scale info
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Scale: %.1f px/m", scale_);
            dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + canvasSize.y - 20),
                IM_COL32(200, 200, 200, 180), buf);
        }
    }

    // --------------------------------------------------------
    // Grid
    // --------------------------------------------------------
    template<typename F>
    void drawGrid(ImDrawList* dl, ImVec2 cp, ImVec2 cs, F& w2s) {
        // Determine grid spacing based on scale
        float gridStep = 10.0f;  // meters
        if (scale_ > 15) gridStep = 5.0f;
        if (scale_ > 30) gridStep = 2.0f;
        if (scale_ < 3)  gridStep = 50.0f;
        if (scale_ < 1.5) gridStep = 100.0f;

        ImU32 gridCol  = IM_COL32(60, 60, 70, 100);
        ImU32 labelCol = IM_COL32(150, 150, 150, 150);

        // Y grid (horizontal lines = range)
        for (float y = -200; y <= 300; y += gridStep) {
            ImVec2 p0 = w2s(-200, y);
            ImVec2 p1 = w2s( 200, y);
            if (p0.y < cp.y || p0.y > cp.y + cs.y) continue;
            dl->AddLine(p0, p1, gridCol);
            char buf[16]; snprintf(buf, sizeof(buf), "%.0fm", y);
            dl->AddText(ImVec2(cp.x + 4, p0.y - 12), labelCol, buf);
        }

        // X grid (vertical lines = lateral)
        for (float x = -200; x <= 200; x += gridStep) {
            ImVec2 p0 = w2s(x, -200);
            ImVec2 p1 = w2s(x,  300);
            if (p0.x < cp.x || p0.x > cp.x + cs.x) continue;
            dl->AddLine(p0, p1, gridCol);
        }
    }

    // --------------------------------------------------------
    // Lanes
    // --------------------------------------------------------
    template<typename F>
    void drawLanes(ImDrawList* dl, F& w2s) {
        std::lock_guard<std::mutex> lk(shared_.mtx);
        for (int i = 0; i < shared_.laneCount; i++) {
            auto& ln = shared_.lanes[i];
            if (!ln.enable) continue;

            float x0 = ln.start_pos;
            float x1 = ln.start_pos + ln.width;

            // Lane fill (semi-transparent) — full Y range
            ImU32 fillCol;
            switch (ln.type) {
            case 1:  fillCol = IM_COL32(40, 60, 120, 50); break;  // Bus
            case 2:  fillCol = IM_COL32(100, 100, 40, 50); break; // Side
            default: fillCol = IM_COL32(50, 70, 90, 45); break;   // Normal
            }

            ImVec2 p0 = w2s(x0, -100);
            ImVec2 p1 = w2s(x1, 300);
            dl->AddRectFilled(
                ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y)),
                ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y)),
                fillCol);

            // Lane border
            dl->AddRect(
                ImVec2(std::min(p0.x, p1.x), std::min(p0.y, p1.y)),
                ImVec2(std::max(p0.x, p1.x), std::max(p0.y, p1.y)),
                IM_COL32(100, 130, 160, 120));

            // Lane label with X range
            char label[64];
            const char* dirStr = (ln.direction == 0) ? "Up" : (ln.direction == 1) ? "Dn" : "UD";
            snprintf(label, sizeof(label), "L%d(%s)", ln.real_lane_num, dirStr);
            ImVec2 mid = w2s((x0 + x1) * 0.5f, 5);
            dl->AddText(ImVec2(mid.x - 15, mid.y), IM_COL32(180, 200, 220, 220), label);

            // X range annotation
            char rangeStr[32];
            snprintf(rangeStr, sizeof(rangeStr), "X: %.1f~%.1fm", x0, x1);
            dl->AddText(ImVec2(mid.x - 25, mid.y + 14), IM_COL32(150, 160, 180, 160), rangeStr);

            // Width annotation
            char wStr[32];
            snprintf(wStr, sizeof(wStr), "W=%.1fm", ln.width);
            dl->AddText(ImVec2(mid.x - 15, mid.y + 28), IM_COL32(150, 160, 180, 140), wStr);
        }
    }

    // --------------------------------------------------------
    // Detect lines
    // --------------------------------------------------------
    template<typename F>
    void drawDetectLines(ImDrawList* dl, ImVec2 cp, ImVec2 cs, F& w2s) {
        float roadMinX = -50, roadMaxX = 50;

        std::lock_guard<std::mutex> lk(shared_.mtx);
        for (int i = 0; i < shared_.detectLineCount; i++) {
            auto& dli = shared_.detectLines[i];
            if (!dli.enable) continue;

            ImU32 col = kDetLineColors[i % MAX_DET_LINES];

            // Detection line
            ImVec2 p0 = w2s(roadMinX, dli.position);
            ImVec2 p1 = w2s(roadMaxX, dli.position);
            dl->AddLine(p0, p1, col, 2.0f);

            // Detection band (semi-transparent rectangle)
            float bandSt = dli.position;
            float bandEnd = dli.position + vdsBandWidth_;
            ImVec2 bp0 = w2s(roadMinX, bandSt);
            ImVec2 bp1 = w2s(roadMaxX, bandEnd);
            ImU32 bandCol = (col & 0x00FFFFFF) | 0x28000000;  // same color, low alpha
            dl->AddRectFilled(
                ImVec2(std::min(bp0.x, bp1.x), std::min(bp0.y, bp1.y)),
                ImVec2(std::max(bp0.x, bp1.x), std::max(bp0.y, bp1.y)),
                bandCol);
            // Also draw the down-direction band
            float bandEndDn = dli.position - vdsBandWidth_;
            ImVec2 bp2 = w2s(roadMinX, bandEndDn);
            ImVec2 bp3 = w2s(roadMaxX, dli.position);
            dl->AddRectFilled(
                ImVec2(std::min(bp2.x, bp3.x), std::min(bp2.y, bp3.y)),
                ImVec2(std::max(bp2.x, bp3.x), std::max(bp2.y, bp3.y)),
                bandCol);

            // Label
            char buf[64];
            snprintf(buf, sizeof(buf), "DL%d (%.1fm)", i + 1, dli.position);
            dl->AddText(ImVec2(p1.x + 4, p0.y - 8), col, buf);
        }

        // Status overlay — lane/detect load status
        {
            char info[256];
            snprintf(info, sizeof(info),
                "Lanes: %d loaded | Detect Lines: %d",
                shared_.laneCount, shared_.detectLineCount);
            dl->AddText(ImVec2(cp.x + 8, cp.y + cs.y - 36),
                IM_COL32(200, 200, 100, 200), info);
        }
    }

    // --------------------------------------------------------
    // Tracks
    // --------------------------------------------------------
    template<typename F>
    void drawTracks(ImDrawList* dl, F& w2s) {
        uint32_t curScan = localScan_.scan_index;

        for (auto& t : localTracks_) {
            ImVec2 pos = w2s(t.x_pos, t.y_pos);

            // Track color by type
            int typeIdx = t.type & 0x07;
            ImU32 col = kTrackColors[typeIdx];
            float radius = 5.0f;

            // Track dot
            dl->AddCircleFilled(pos, radius, col);

            // Velocity vector
            float velScale = scale_ * 0.3f;
            ImVec2 velEnd(pos.x + t.x_vel * velScale,
                          pos.y - t.y_vel * velScale);
            dl->AddLine(pos, velEnd, IM_COL32(255, 255, 255, 120), 1.5f);

            // VDS highlight: pulsing ring for tracks that triggered VDS recently
            int trkIdx = t.id & 0xFF;
            uint32_t hlScan = vdsHighlightTracks_[trkIdx];
            if (hlScan > 0) {
                uint32_t age = curScan - hlScan;
                if (age < 60) {  // ~2 seconds at ~30 scans/sec
                    // Pulsing effect: oscillate radius and alpha
                    float phase = (float)(age % 20) / 20.0f;
                    float pulse = 8.0f + 6.0f * std::sin(phase * 6.2832f);
                    int alpha = 220 - (int)(age * 3);
                    if (alpha < 40) alpha = 40;
                    dl->AddCircle(pos, pulse, IM_COL32(255, 50, 50, alpha), 0, 2.5f);
                } else {
                    vdsHighlightTracks_[trkIdx] = 0;  // expire
                }
            }

            // ID label
            char idBuf[16];
            snprintf(idBuf, sizeof(idBuf), "%d", t.id);
            dl->AddText(ImVec2(pos.x + 8, pos.y - 12), col, idBuf);
        }
    }

    // --------------------------------------------------------
    // Legend
    // --------------------------------------------------------
    void drawLegend(ImDrawList* dl, ImVec2 cp) {
        float x = cp.x + 10;
        float y = cp.y + 10;
        ImU32 bg = IM_COL32(30, 30, 40, 200);

        int legendEntries = 2;  // Track + at least one det line label
        std::lock_guard<std::mutex> lk(shared_.mtx);
        int dlCount = shared_.detectLineCount;
        int enabledLines = 0;
        for (int i = 0; i < dlCount; i++) {
            if (shared_.detectLines[i].enable) enabledLines++;
        }
        legendEntries += enabledLines;

        float legendH = 28.0f + legendEntries * 16.0f;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 160, y + legendH), bg, 4.0f);
        dl->AddText(ImVec2(x + 6, y + 4), IM_COL32(255,255,255,220), "Legend");

        y += 20;
        dl->AddCircleFilled(ImVec2(x+16, y+6), 4.0f, IM_COL32(255,255,0,255));
        dl->AddText(ImVec2(x+32, y), IM_COL32(200,200,200,200), "Track");

        y += 16;
        // One entry per enabled detect line
        for (int i = 0; i < dlCount; i++) {
            if (!shared_.detectLines[i].enable) continue;
            ImU32 col = kDetLineColors[i % MAX_DET_LINES];
            dl->AddLine(ImVec2(x+6,y+6), ImVec2(x+26,y+6), col, 2.0f);
            char label[32];
            snprintf(label, sizeof(label), "Det Line %d", i + 1);
            dl->AddText(ImVec2(x+32, y), IM_COL32(200,200,200,200), label);
            y += 16;
        }
    }

    // --------------------------------------------------------
    // Connection
    // --------------------------------------------------------
    void doConnect() {
        state_ = AppState::Connecting;

        handle_ = byda_create();
        if (!handle_) {
            state_ = AppState::Disconnected;
            return;
        }

        byda_set_address(handle_, ipBuf_, port_);

        int rc = byda_connect(handle_);
        if (rc != BYDA_OK) {
            byda_destroy(handle_);
            handle_ = nullptr;
            state_ = AppState::Disconnected;
            return;
        }

        {
            std::lock_guard<std::mutex> lk(shared_.mtx);
            shared_.connected = true;
        }

        // Init lane editor
        maxLanes_ = byda_get_max_lanes(handle_);
        editLaneCount_ = 0;
        lanesLoaded_   = false;

        // Init detect line editor
        editDetLineCount_ = 0;
        detLinesLoaded_    = false;

        state_ = AppState::LoadingParams;
        startReceiveThread();
    }

    // --------------------------------------------------------
    // Members
    // --------------------------------------------------------
    GLFWwindow*      window_  = nullptr;
    BydaRadarHandle  handle_  = nullptr;
    std::atomic<AppState> state_{AppState::Disconnected};

    // Connection settings
    char ipBuf_[64]{};
    int  port_ = 7;

    // Receive thread
    std::thread      recvThread_;
    std::atomic<bool> recvRunning_{false};
    SharedRadarData  shared_;

    // Local copies (main thread only)
    BydaScanInfo              localScan_{};
    std::vector<BydaTrack>    localTracks_;

    // Lane editor (UI side)
    BydaLaneInfo editLanes_[MAX_LANES]{};
    int          editLaneCount_ = 0;
    int          maxLanes_      = 13;
    bool         lanesLoaded_   = false;

    // Pending lane save (for thread-safe apply)
    std::mutex   laneSaveMtx_;
    BydaLaneInfo pendingLanes_[MAX_LANES]{};
    int          pendingLaneCount_ = 0;
    std::atomic<bool> saveLanes_{false};

    // Detect line editor (UI side)
    BydaDetectLineInfo editDetLines_[MAX_DET_LINES]{};
    int                editDetLineCount_ = 0;
    bool               detLinesLoaded_   = false;

    // Pending detect line save (for thread-safe apply)
    std::mutex         detLineSaveMtx_;
    BydaDetectLineInfo pendingDetLines_[MAX_DET_LINES]{};
    int                pendingDetLineCount_ = 0;
    std::atomic<bool>  saveDetLines_{false};

    // 2D view transform
    float scale_ = 6.0f;   // pixels per meter
    float panX_  = 0.0f;
    float panY_  = 0.0f;

    // VDS detection (local, not saved to radar)
    bool  vdsEnabled_      = true;
    float vdsBandWidth_    = 3.0f;   // meters
    float vdsCar2CarTime_  = 0.8f;   // seconds

    bool     passTrkList_[MAX_DET_LINES][MAX_PASS_TRK_ID]{};
    int      lastVdsScan_[MAX_DET_LINES][MAX_LANES]{};
    uint32_t vdsHighlightTracks_[MAX_PASS_TRK_ID]{};

    std::deque<VdsEvent> vdsEvents_;
};

// ============================================================
// Entry point
// ============================================================
int main(int, char**) {
#ifdef _WIN32
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif

#ifdef HAS_SPDLOG
    Logger::init();
#endif
    LOG_INFO("BSR20 Radar Viewer started");

    RadarViewerApp app;
    int rc = app.run();

#ifdef HAS_SPDLOG
    Logger::shutdown();
#endif
    return rc;
}
