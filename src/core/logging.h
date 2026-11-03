#pragma once

#include <spdlog/spdlog.h>

#include <cstddef>
#include <memory>
#include <string>

namespace msl {
namespace log {

/// Log channels. Each channel writes to its own folder:
///   <base>/lidar/lidar.log, <base>/radar/radar.log, ...
/// so a problem with one sensor can be inspected without noise from the rest.
enum class Channel {
    Lidar = 0,
    Radar,
    Camera1,
    Camera2,
    Gps,
    Ui,
    Recorder,
    Count
};

struct Options {
    /// Base directory for log folders. Empty = "<executable dir>/logs".
    std::string dir;

    /// Total disk budget per channel (all rotated files together).
    /// Default 1 GiB per channel.
    std::size_t max_bytes_per_channel = 1024ull * 1024ull * 1024ull;

    /// Number of rotated files per channel. Each file is
    /// max_bytes_per_channel / files_per_channel bytes.
    std::size_t files_per_channel = 4;

    /// File sink level (kept at debug so post-mortem analysis is possible).
    spdlog::level::level_enum file_level = spdlog::level::debug;

    /// Console sink level (info by default, debug in developer mode).
    spdlog::level::level_enum console_level = spdlog::level::info;
};

/// Create all channel loggers. Safe to call once at startup.
/// Falls back to console-only logging if the log directory cannot be created.
void init(const Options& opts = Options{});

/// Get a channel logger. Always returns a valid logger, even before init().
const std::shared_ptr<spdlog::logger>& get(Channel ch);

/// Camera channel by slot index (0 -> camera1, 1 -> camera2).
const std::shared_ptr<spdlog::logger>& camera(int slot);

inline const std::shared_ptr<spdlog::logger>& lidar()    { return get(Channel::Lidar); }
inline const std::shared_ptr<spdlog::logger>& radar()    { return get(Channel::Radar); }
inline const std::shared_ptr<spdlog::logger>& gps()      { return get(Channel::Gps); }
inline const std::shared_ptr<spdlog::logger>& ui()       { return get(Channel::Ui); }
inline const std::shared_ptr<spdlog::logger>& recorder() { return get(Channel::Recorder); }

/// Channel name as used for the folder/file name ("lidar", "camera1", ...).
const char* name(Channel ch);

/// Change the console verbosity at runtime (developer mode).
/// File sinks always stay at their configured level.
void setConsoleLevel(spdlog::level::level_enum lvl);

/// Directory actually used for log files (empty if console-only).
const std::string& baseDir();

/// Flush every channel (called on recording stop and at exit).
void flushAll();

/// Flush and release all loggers.
void shutdown();

} // namespace log
} // namespace msl
