#pragma once

#include "core/thread_safe_queue.h"
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>

namespace msl {

/// Abstract base class for file loggers with queue-based async writing.
/// Mirrors Python BaseLogger(QThread) pattern exactly:
/// - enqueue() pushes data (drops oldest if full)
/// - Writer thread: onStart() → drain loop → onStop()
/// - stop() sets running_=false, drains remaining items, joins (10s timeout)
template<typename T, size_t MaxSize = 500>
class BaseLogger {
public:
    virtual ~BaseLogger() { stop(); }

    /// Push data to the write queue. Thread-safe.
    void enqueue(T item) {
        queue_.try_push(std::move(item));
    }

    /// Start the writer thread.
    void start() {
        if (running_) return;
        running_ = true;
        thread_ = std::thread([this]() { run(); });
    }

    /// Stop the writer thread. Drains remaining queue items.
    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// Check if the logger is running.
    bool isRunning() const { return running_; }

    /// Get current queue depth.
    size_t queueSize() const { return queue_.size(); }

protected:
    /// Called once when writer thread starts. Open files here.
    virtual void onStart() = 0;

    /// Called for each dequeued item. Write to file here.
    virtual void writeItem(const T& item) = 0;

    /// Called once when writer thread stops. Close files here.
    virtual void onStop() = 0;

private:
    ThreadSafeQueue<T, MaxSize> queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void run() {
        onStart();

        while (running_ || !queue_.empty()) {
            T item;
            if (queue_.try_pop(item, std::chrono::milliseconds(500))) {
                try {
                    writeItem(item);
                } catch (const std::exception& e) {
                    spdlog::error("Logger write error: {}", e.what());
                }
            }
        }

        onStop();
    }
};

} // namespace msl
