#pragma once
#include "sim/core/simulation.hpp"
#include "sim/runtime/bounded_queue.hpp"
#include <future>
#include <memory>
#include <thread>

namespace sim::runtime {
class QueueFull : public std::runtime_error {
public:
    QueueFull() : std::runtime_error("Scheduler queue full; request not accepted") {}
};
using ResultPtr = std::shared_ptr<const core::RunResult>;
class Runner {
public:
    // Up to eight queued runs, plus one active run.
    explicit Runner(std::size_t capacity = 2);
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;
    std::future<ResultPtr> submit(core::RunSpec spec);
private:
    struct Job {
        core::RunSpec spec;
        std::promise<ResultPtr> promise;
    };
    void work(std::stop_token stop);
    BoundedQueue<Job> jobs_;
    std::jthread thread_; // Last: referenced state exists before thread starts.
};
}
