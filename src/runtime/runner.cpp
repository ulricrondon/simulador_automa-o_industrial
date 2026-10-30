#include "sim/runtime/runner.hpp"
#include "sim/core/conveyor.hpp"

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
    return enqueue(Job{std::move(spec), {}, std::nullopt});
}
std::future<ResultPtr> Runner::submit_controlled(core::RunSpec spec, std::vector<core::ControlAction> actions) {
    core::validate(spec);
    if (!spec.conveyor || actions.size() > 100000) {
        throw std::invalid_argument("Controlled run requires conveyor and at most 100000 actions");
    }
    for (auto action : actions) {
        if (action != core::ControlAction::step && action != core::ControlAction::pause &&
            action != core::ControlAction::resume && action != core::ControlAction::stop) {
            throw std::invalid_argument("Invalid control action");
        }
    }
    return enqueue(Job{std::move(spec), {}, std::move(actions)});
}
std::future<ResultPtr> Runner::enqueue(Job job) {
    auto future = job.promise.get_future();
    if (!jobs_.try_push(std::move(job))) { throw QueueFull{}; }
    return future;
}
void Runner::work(std::stop_token stop) {
    while (auto job = jobs_.pop(stop)) {
        try {
            if (job->actions) {
                core::ConveyorSession session(job->spec);
                for (auto action : *job->actions) {
                    if (stop.stop_requested()) { throw core::RunCancelled{}; }
                    switch (action) {
                    case core::ControlAction::step: session.step(); break;
                    case core::ControlAction::pause: session.pause(); break;
                    case core::ControlAction::resume: session.resume(); break;
                    case core::ControlAction::stop: session.stop(); break;
                    }
                }
                if (stop.stop_requested()) { throw core::RunCancelled{}; }
                const core::SessionStatus status{session.state(), session.next_tick(),
                                                 session.position_um(), session.motor_state()};
                auto result = std::move(session).result();
                result.control = status;
                job->promise.set_value(std::make_shared<const core::RunResult>(std::move(result)));
            } else {
                job->promise.set_value(std::make_shared<const core::RunResult>(core::simulate(job->spec, stop)));
            }
        } catch (...) {
            job->promise.set_exception(std::current_exception());
        }
    }
    while (auto job = jobs_.try_pop()) {
        job->promise.set_exception(std::make_exception_ptr(core::RunCancelled{}));
    }
}
}
