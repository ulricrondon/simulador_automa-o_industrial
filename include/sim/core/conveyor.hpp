#pragma once
#include "sim/core/simulation.hpp"

namespace sim::core {
// Single-owner session: controls apply only between complete simulated steps.
// It performs no waiting, background scheduling or wall-clock measurement.
class ConveyorSession {
public:
    explicit ConveyorSession(RunSpec spec);
    bool step();
    void pause();
    void resume();
    void stop();
    SessionState state() const noexcept { return state_; }
    Tick next_tick() const noexcept { return tick_; }
    std::int64_t position_um() const noexcept { return position_; }
    DigitalState motor_state() const noexcept { return live_.motor_state; }
    const RunResult& result() const noexcept { return result_; }
private:
    void record(Phase phase, TransitionKind kind, std::uint32_t index,
                DigitalState value, bool end_of_step = false);
    RunSpec spec_;
    SessionState state_{SessionState::running};
    Tick tick_{};
    std::size_t next_event_{};
    std::int64_t position_{};
    std::int64_t remainder_{};
    ConveyorSnapshot live_;
    std::vector<TimerState> timers_;
    RunResult result_;
};
void validate_conveyor(const RunSpec& spec);
RunResult simulate_conveyor(const RunSpec& spec, std::stop_token stop);
RunSpec conveyor_scenario();
}
