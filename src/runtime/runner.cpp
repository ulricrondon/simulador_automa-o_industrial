#include "sim/runtime/runner.hpp"

namespace sim::runtime {
namespace {
std::size_t checked_capacity(std::size_t capacity) {
    if (capacity == 0 || capacity > 8) { throw std::invalid_argument("Run queue capacity must be in [1, 8]"); }
    return capacity;
}
}
Runner::Runner(std::size_t capacity)
    : jobs_(checked_capacity(capacity)), thread_([this](std::stop_token stop) { work(stop); }) {}
Runner::~Runner() {
    thread_.request_stop();
    jobs_.close();
    thread_.join();
}
std::future<ResultPtr> Runner::submit(core::RunSpec spec) {
    core::validate(spec);
    Job job{std::move(spec), {}};
    auto future = job.promise.get_future();
    if (!jobs_.try_push(std::move(job))) { throw QueueFull{}; }
    return future;
}
void Runner::work(std::stop_token stop) {
    while (auto job = jobs_.pop(stop)) {
        try {
            job->promise.set_value(std::make_shared<const core::RunResult>(core::simulate(job->spec, stop)));
        } catch (...) {
            job->promise.set_exception(std::current_exception());
        }
    }
    while (auto job = jobs_.try_pop()) {
        job->promise.set_exception(std::make_exception_ptr(core::RunCancelled{}));
    }
}
}
