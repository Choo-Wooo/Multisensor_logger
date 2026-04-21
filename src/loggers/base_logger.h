#pragma once

#include "core/thread_safe_queue.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <utility>
#include <spdlog/spdlog.h>

namespace msl {

/// Abstract base class for file loggers with queue-based async writing.
/// Mirrors Python BaseLogger(QThread) pattern exactly:
/// - enqueue() pushes data (drops oldest if full)
/// - Writer thread: onStart() -> drain loop -> onStop()
/// - stop() sets running_=false, drains remaining items, joins (10s timeout)
template<typename T, size_t MaxSize = 500>
class BaseLogger {
public:
    virtual ~BaseLogger() { stop(); }

    /// Push data to the write queue. Thread-safe.
    void enqueue(T item) {
        bool dropped = queue_.try_push(std::move(item));
        size_t depth = queue_.size();
        updateMaxQueueDepth(depth);

        if (dropped) {
            uint64_t drops = dropped_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            if (drops == 1 || drops % 100 == 0) {
                spdlog::warn("{} queue overflow: dropped oldest queued item (drops={}, depth={})",
                             loggerName(), drops, depth);
            }
        }
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

    virtual const char* loggerName() const { return "BaseLogger"; }

private:
    ThreadSafeQueue<T, MaxSize> queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> dropped_count_{0};
    std::atomic<size_t> max_queue_depth_{0};

    void run() {
        using steady_clock = std::chrono::steady_clock;

        onStart();

        auto last_report = steady_clock::now();
        uint64_t report_items = 0;
        double report_total_ms = 0.0;
        double report_max_ms = 0.0;

        while (running_ || !queue_.empty()) {
            T item;
            if (queue_.try_pop(item, std::chrono::milliseconds(500))) {
                auto write_begin = steady_clock::now();
                try {
                    writeItem(item);
                } catch (const std::exception& e) {
                    spdlog::error("Logger write error: {}", e.what());
                }

                double write_ms =
                    std::chrono::duration<double, std::milli>(steady_clock::now() - write_begin).count();
                report_items++;
                report_total_ms += write_ms;
                if (write_ms > report_max_ms) {
                    report_max_ms = write_ms;
                }

                auto now = steady_clock::now();
                if (std::chrono::duration<double>(now - last_report).count() >= 2.0) {
                    double avg_ms = report_items > 0 ? report_total_ms / static_cast<double>(report_items) : 0.0;
                    spdlog::debug(
                        "{} perf: items={}, avg_write_ms={:.3f}, max_write_ms={:.3f}, queue={}, max_queue={}, dropped={}",
                        loggerName(),
                        report_items,
                        avg_ms,
                        report_max_ms,
                        queue_.size(),
                        max_queue_depth_.load(std::memory_order_relaxed),
                        dropped_count_.load(std::memory_order_relaxed));
                    last_report = now;
                    report_items = 0;
                    report_total_ms = 0.0;
                    report_max_ms = 0.0;
                }
            }
        }

        onStop();
        spdlog::debug("{} summary: max_queue={}, dropped={}",
                      loggerName(),
                      max_queue_depth_.load(std::memory_order_relaxed),
                      dropped_count_.load(std::memory_order_relaxed));
    }

    void updateMaxQueueDepth(size_t depth) {
        size_t current = max_queue_depth_.load(std::memory_order_relaxed);
        while (depth > current &&
               !max_queue_depth_.compare_exchange_weak(current, depth, std::memory_order_relaxed)) {
        }
    }
};

} // namespace msl
