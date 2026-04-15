#pragma once

#include <mutex>
#include <atomic>
#include <memory>

namespace msl {

/// Thread-safe "latest value" holder.
/// Writer overwrites the latest value, reader takes it.
/// No queue, no accumulation — only the most recent value is kept.
/// Ideal for high-frequency sensor data (camera frames, lidar scans).
template<typename T>
class LatestValue {
public:
    /// Write a new value (any thread). Overwrites previous.
    void write(std::shared_ptr<T> val) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_ = std::move(val);
        has_new_ = true;
    }

    /// Write by moving value.
    void write(T&& val) {
        write(std::make_shared<T>(std::move(val)));
    }

    /// Try to read the latest value. Returns true if new data was available.
    /// Clears the "new" flag after reading.
    bool tryRead(std::shared_ptr<T>& out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!has_new_ || !data_) return false;
        out = data_;
        has_new_ = false;
        return true;
    }

    bool hasNew() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return has_new_;
    }

private:
    mutable std::mutex mtx_;
    std::shared_ptr<T> data_;
    bool has_new_ = false;
};

} // namespace msl
