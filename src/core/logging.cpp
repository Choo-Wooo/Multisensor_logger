#include "logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace msl {
namespace log {

namespace {

constexpr int kChannelCount = static_cast<int>(Channel::Count);

const char* kChannelNames[kChannelCount] = {
    "lidar", "radar", "camera1", "camera2", "gps", "ui", "recorder"
};

std::array<std::shared_ptr<spdlog::logger>, kChannelCount> g_loggers{};
std::shared_ptr<spdlog::sinks::sink> g_console_sink;
std::string g_base_dir;
std::once_flag g_fallback_once;
bool g_initialized = false;

/// Directory of the running executable (logs live next to it, like Data/).
std::string executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return std::filesystem::path(buf).parent_path().string();
    }
#else
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::filesystem::path(buf).parent_path().string();
    }
#endif
    return std::filesystem::current_path().string();
}

/// Writable fallback location when the executable directory is read-only
/// (e.g. installed under C:\Program Files).
std::string userDataDir() {
#ifdef _WIN32
    const char* base = std::getenv("LOCALAPPDATA");
    if (!base) base = std::getenv("APPDATA");
    if (base && *base) {
        return (std::filesystem::path(base) / "MultisensorLogger").string();
    }
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return (std::filesystem::path(home) / ".local" / "share" / "MultisensorLogger").string();
    }
#endif
    return std::filesystem::temp_directory_path().string();
}

/// Can we create files in this directory?
bool dirWritable(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;
    std::filesystem::path probe = std::filesystem::path(dir) / ".write_probe";
    {
        std::ofstream f(probe);
        if (!f.is_open()) return false;
        f << "ok";
    }
    std::filesystem::remove(probe, ec);
    return true;
}

std::shared_ptr<spdlog::sinks::sink> consoleSink(spdlog::level::level_enum lvl) {
    if (!g_console_sink) {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        g_console_sink = sink;
    }
    g_console_sink->set_level(lvl);
    return g_console_sink;
}

/// Console-only logger used when init() has not run yet, or when the log
/// directory is not writable. Never returns null so call sites stay simple.
void ensureFallback() {
    std::call_once(g_fallback_once, [] {
        auto sink = consoleSink(spdlog::level::info);
        for (int i = 0; i < kChannelCount; ++i) {
            if (g_loggers[i]) continue;
            auto logger = std::make_shared<spdlog::logger>(kChannelNames[i], sink);
            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::warn);
            g_loggers[i] = logger;
        }
    });
    for (int i = 0; i < kChannelCount; ++i) {
        if (!g_loggers[i]) {
            g_loggers[i] = std::make_shared<spdlog::logger>(
                kChannelNames[i], consoleSink(spdlog::level::info));
        }
    }
}

} // namespace

const char* name(Channel ch) {
    int idx = static_cast<int>(ch);
    if (idx < 0 || idx >= kChannelCount) return "ui";
    return kChannelNames[idx];
}

void init(const Options& opts) {
    if (g_initialized) return;
    g_initialized = true;

    std::string base;
    if (!opts.dir.empty()) {
        base = opts.dir;
    } else {
        // 1st choice: next to the executable (portable/tarball install).
        // 2nd choice: per-user data dir (installed under Program Files).
        base = (std::filesystem::path(executableDir()) / "logs").string();
        if (!dirWritable(base)) {
            base = (std::filesystem::path(userDataDir()) / "logs").string();
        }
    }

    std::size_t files = opts.files_per_channel ? opts.files_per_channel : 1;
    std::size_t per_file = opts.max_bytes_per_channel / files;
    if (per_file < 1024ull * 1024ull) per_file = 1024ull * 1024ull;  // >= 1 MiB

    auto console = consoleSink(opts.console_level);

    bool dir_ok = true;
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) dir_ok = false;

    for (int i = 0; i < kChannelCount && dir_ok; ++i) {
        const char* cname = kChannelNames[i];
        std::filesystem::path folder = std::filesystem::path(base) / cname;
        std::filesystem::create_directories(folder, ec);
        if (ec) { dir_ok = false; break; }

        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(console);
        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (folder / (std::string(cname) + ".log")).string(),
                per_file, files, /*rotate_on_open=*/false);
            file_sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [%L] [%n] [t%t] %v");
            file_sink->set_level(opts.file_level);
            sinks.push_back(file_sink);
        } catch (const std::exception&) {
            dir_ok = false;
            break;
        }

        auto logger = std::make_shared<spdlog::logger>(cname, sinks.begin(), sinks.end());
        logger->set_level(std::min(opts.file_level, opts.console_level));
        logger->flush_on(spdlog::level::warn);
        g_loggers[i] = logger;
    }

    if (!dir_ok) {
        // Keep running with console-only logging rather than losing messages.
        for (int i = 0; i < kChannelCount; ++i) g_loggers[i].reset();
        ensureFallback();
        g_base_dir.clear();
        spdlog::set_default_logger(g_loggers[static_cast<int>(Channel::Ui)]);
        spdlog::warn("Logging: cannot write to '{}' — console-only logging", base);
        return;
    }

    g_base_dir = base;

    // Anything still using the plain spdlog::xxx() API lands in the ui channel.
    spdlog::set_default_logger(g_loggers[static_cast<int>(Channel::Ui)]);
    spdlog::flush_every(std::chrono::seconds(3));

    get(Channel::Ui)->info("Logging: {} channels -> {} ({} files x {:.0f} MiB per channel)",
                           kChannelCount, base, files,
                           static_cast<double>(per_file) / (1024.0 * 1024.0));
}

const std::shared_ptr<spdlog::logger>& get(Channel ch) {
    int idx = static_cast<int>(ch);
    if (idx < 0 || idx >= kChannelCount) idx = static_cast<int>(Channel::Ui);
    if (!g_loggers[idx]) ensureFallback();
    return g_loggers[idx];
}

const std::shared_ptr<spdlog::logger>& camera(int slot) {
    return get(slot <= 0 ? Channel::Camera1 : Channel::Camera2);
}

void setConsoleLevel(spdlog::level::level_enum lvl) {
    if (g_console_sink) g_console_sink->set_level(lvl);
    for (int i = 0; i < kChannelCount; ++i) {
        if (g_loggers[i]) g_loggers[i]->set_level(std::min(g_loggers[i]->level(), lvl));
    }
}

const std::string& baseDir() { return g_base_dir; }

void flushAll() {
    for (int i = 0; i < kChannelCount; ++i) {
        if (g_loggers[i]) g_loggers[i]->flush();
    }
}

void shutdown() {
    flushAll();
    for (int i = 0; i < kChannelCount; ++i) g_loggers[i].reset();
    g_console_sink.reset();
    spdlog::shutdown();
}

} // namespace log
} // namespace msl
