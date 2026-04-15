#pragma once

#include <chrono>
#include <ctime>
#include <string>
#include <cstdio>

namespace msl {

class Clock {
public:
    // Get current time as Unix timestamp (seconds with microsecond precision)
    static double now() {
        auto tp = std::chrono::system_clock::now();
        auto dur = tp.time_since_epoch();
        return std::chrono::duration<double>(dur).count();
    }

    // Get steady clock time for relative measurements (not affected by wall clock changes)
    static double steady() {
        auto tp = std::chrono::steady_clock::now();
        auto dur = tp.time_since_epoch();
        return std::chrono::duration<double>(dur).count();
    }

    // Calculate pc_ts_rel from recording start time
    static double relativeTime(double recording_start) {
        return now() - recording_start;
    }

    // Format Unix timestamp to ISO 8601 UTC string
    // e.g., "2026-04-01T15:00:12.123456"
    static std::string toUtcString(double unix_ts) {
        auto sec = static_cast<time_t>(unix_ts);
        auto usec = static_cast<int>((unix_ts - sec) * 1000000);
        struct tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &sec);
#else
        gmtime_r(&sec, &utc);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06d",
                      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                      utc.tm_hour, utc.tm_min, utc.tm_sec, usec);
        return buf;
    }

    // Format seconds to "HH:MM:SS" display string
    static std::string formatDuration(double seconds) {
        int total = static_cast<int>(seconds);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        return buf;
    }

    // Format seconds to "HH:MM:SS.mmm" display string
    static std::string formatDurationMs(double seconds) {
        int total_ms = static_cast<int>(seconds * 1000);
        int h = total_ms / 3600000;
        int m = (total_ms % 3600000) / 60000;
        int s = (total_ms % 60000) / 1000;
        int ms = total_ms % 1000;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
        return buf;
    }

    // Generate session name like "rec_20260401_150012"
    static std::string generateSessionName() {
        auto tp = std::chrono::system_clock::now();
        auto sec = std::chrono::system_clock::to_time_t(tp);
        struct tm local{};
#ifdef _WIN32
        localtime_s(&local, &sec);
#else
        localtime_r(&sec, &local);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "rec_%04d%02d%02d_%02d%02d%02d",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      local.tm_hour, local.tm_min, local.tm_sec);
        return buf;
    }
};

} // namespace msl
