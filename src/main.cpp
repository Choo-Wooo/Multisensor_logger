#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <spdlog/spdlog.h>

#include "ui/main_window.h"

extern "C" {
#include <libavutil/log.h>
}

#define NMEA_DLL
#include <nmea.h>

static void glfwErrorCallback(int error, const char* description) {
    spdlog::error("GLFW Error {}: {}", error, description);
}

int main(int /*argc*/, char** /*argv*/) {
    // --- spdlog ---
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Multi-Sensor Logger starting...");

    // --- GLFW ---
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
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
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // --- GLAD ---
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        spdlog::error("Failed to initialize GLAD");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    spdlog::info("OpenGL: {}", (const char*)glGetString(GL_VERSION));

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

    spdlog::info("Multi-Sensor Logger exited cleanly");
    return 0;
}
