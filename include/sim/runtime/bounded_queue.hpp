#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace sim::runtime {
template<class T> class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) { throw std::invalid_argument("Queue capacity must be positive"); }
    }
    bool try_push(T value) {
        std::lock_guard lock(mutex_);
        if (closed_ || queue_.size() == capacity_) { return false; }
        queue_.push_back(std::move(value));
        ready_.notify_one();
        return true;
    }
    std::optional<T> pop(std::stop_token stop) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop, [this] { return closed_ || !queue_.empty(); });
        if (stop.stop_requested() || queue_.empty()) { return std::nullopt; }
        auto value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) { return std::nullopt; }
        auto value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        ready_.notify_all();
    }
private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::deque<T> queue_;
    bool closed_{};
};
}
