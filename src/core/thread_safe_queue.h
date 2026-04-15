#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstddef>

namespace msl {

/// Bounded, thread-safe MPSC queue.
/// When full, try_push drops the oldest item (front) before pushing.
/// Mirrors Python BaseLogger's queue.Queue(maxsize=500) behavior.
template<typename T, size_t MaxSize = 500>
class ThreadSafeQueue {
public:
    /// Push an item. If the queue is full, drop the oldest item first.
    /// Returns true if an item was dropped to make room.
    bool try_push(T item) {
        std::lock_guard<std::mutex> lock(mtx_);
        bool dropped = false;
        if (queue_.size() >= MaxSize) {
            queue_.pop_front();
            dropped = true;
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return dropped;
    }

    /// Try to pop an item with a timeout.
    /// Returns true if an item was popped, false on timeout.
    bool try_pop(T& out, std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /// Non-blocking pop. Returns true if an item was available.
    bool try_pop_nowait(T& out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.clear();
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<T> queue_;
};

} // namespace msl
