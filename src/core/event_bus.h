#pragma once

#include <functional>
#include <vector>
#include <mutex>

namespace msl {

/// Cross-thread event bus replacing Qt Signal/Slot.
/// Worker threads post lambdas via post(), main thread drains via drain().
class EventBus {
public:
    /// Post an event from any thread (thread-safe).
    /// Returns false if queue is full (back-pressure).
    bool post(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (pending_.size() >= max_pending_) {
            return false;  // Drop event — back-pressure
        }
        pending_.push_back(std::move(fn));
        return true;
    }

    /// Drain and execute all pending events on the calling thread.
    /// Should be called once per frame from the main thread.
    void drain() {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            batch.swap(pending_);
        }
        for (auto& fn : batch) {
            fn();
        }
    }

    /// Check if there are pending events.
    bool hasPending() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return !pending_.empty();
    }

    /// Get number of pending events.
    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return pending_.size();
    }

    /// Clear all pending events without executing them.
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        pending_.clear();
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::function<void()>> pending_;
    static constexpr size_t max_pending_ = 60;  // Back-pressure limit
};

} // namespace msl
