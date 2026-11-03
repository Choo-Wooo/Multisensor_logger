#include <glad/glad.h>
#include "core/logging.h"
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <spdlog/spdlog.h>

#include "ui/main_window.h"

#ifndef MSL_VERSION_STRING
#define MSL_VERSION_STRING "dev"
#endif

extern "C" {
#include <libavutil/log.h>
}

#define NMEA_DLL
#include <nmea.h>

static void glfwErrorCallback(int error, const char* description) {
    msl::log::ui()->error("GLFW Error {}: {}", error, description);
}

int main(int /*argc*/, char** /*argv*/) {
    // --- Logging ---
    // Per-channel rotating log files: <exe dir>/logs/<channel>/<channel>.log
    // Channels: lidar, radar, camera1, camera2, gps, ui, recorder.
    // Each channel keeps at most 1 GiB on disk (4 rotated files).
    // Console stays at info; files always record debug so a silent stall can
    // be analysed after the fact.
    {
        msl::log::Options log_opts;
        log_opts.max_bytes_per_channel = 1024ull * 1024ull * 1024ull;  // 1 GiB
        log_opts.files_per_channel     = 4;
        log_opts.file_level            = spdlog::level::debug;
        log_opts.console_level         = spdlog::level::info;
        msl::log::init(log_opts);
    }
    msl::log::ui()->info("Multi-Sensor Logger {} starting...", MSL_VERSION_STRING);

    // --- GLFW ---
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        msl::log::ui()->error("Failed to initialize GLFW");
        return 1;
    }

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 1000, "Multi-Sensor Logger", nullptr, nullptr);
    if (!window) {
        msl::log::ui()->error("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // --- GLAD ---
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        msl::log::ui()->error("Failed to initialize GLAD");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    msl::log::ui()->info("OpenGL: {}", (const char*)glGetString(GL_VERSION));

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding  = 3.0f;
    style.GrabRounding   = 3.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- FFmpeg log level (suppress "no frame!" warnings) ---
    av_log_set_level(AV_LOG_ERROR);

    // --- libnmea ---
    nmea_init();

    // --- Application ---
    msl::MainWindow app;
    app.init();

    // --- Main Loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.update();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- Shutdown ---
    app.shutdown();

    nmea_cleanup();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    msl::log::ui()->info("Multi-Sensor Logger exited cleanly");
    msl::log::shutdown();
    return 0;
}
