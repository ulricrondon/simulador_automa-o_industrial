#pragma once
#include "sim/core/conveyor_model.hpp"
#include <optional>
#include <string_view>
#include <cstdint>
#include <stop_token>
#include <stdexcept>
#include <vector>

namespace sim::core {
using Tick = std::uint64_t;
using Signal = std::uint32_t;
enum class Bank { input, output };
enum class ComparisonOp { equal, not_equal, less, less_equal, greater, greater_equal };
struct CounterReference { Signal coil{}; };
struct CvComparison {
    CounterReference counter;
    ComparisonOp op{ComparisonOp::equal};
    std::int64_t constant{};
};
struct Contact {
    Bank bank{Bank::input};
    Signal signal{};
    bool normally_closed{};
    std::optional<CvComparison> comparison{};
    static Contact compare(CvComparison value) { return {Bank::input, 0, false, value}; }
};
struct CounterConfig {
    std::int64_t pv{};
    Contact reset; // R reads the same input/previous-output image as CU.
};
using Branch = std::vector<Contact>; // Series contacts: AND.
struct Rung {
    std::vector<Branch> branches; // Parallel branches: OR.
    Signal coil{};
    std::optional<std::uint64_t> ton_us{};
    std::optional<CounterConfig> ctu{}; // Instance identity = rung UUID; runtime slot = coil.
};
struct Program {
    std::uint32_t input_count{1};
    std::uint32_t output_count{1};
    std::vector<Rung> rungs;
};
struct Axis {
    Signal drive{};
    std::int64_t initial_um{};
    std::int64_t maximum_um{1000};
    std::int64_t travel_um_per_tick{10};
};
struct InputEvent {
    Tick tick{};
    std::uint64_t sequence{}; // Unique per run, assigned before dispatch.
    Signal signal{};
    bool value{};
};
struct RunSpec {
    Program program;
    Axis axis;
    Tick ticks{1};
    std::uint64_t tick_period_us{1000};
    std::vector<InputEvent> events;
    std::optional<Conveyor> conveyor;
};
struct TimerState {
    std::uint64_t elapsed_us{};
    bool active{};
    DigitalState q{DigitalState::LOW};
    bool operator==(const TimerState&) const = default;
};
struct CounterState {
    std::int64_t cv{};
    bool armed{}; // A valid LOW with R=LOW has been observed.
    DigitalState q{DigitalState::LOW};
    bool operator==(const CounterState&) const = default;
};
struct CounterEvent {
    Tick tick{};
    std::uint64_t time_us{};
    Signal coil{};
    CounterState state;
    bool stopped{};
    bool operator==(const CounterEvent&) const = default;
};
CounterState update_ctu(CounterState state, DigitalState cu, DigitalState reset, std::int64_t pv);
DigitalState evaluate_comparison(const CounterState& counter, const CvComparison& comparison);
std::string_view comparison_name(ComparisonOp op);
bool has_comparisons(const Program& program);
void validate_comparisons(const Program& program);
bool has_counters(const Program& program);
bool has_timers(const Program& program);
struct Snapshot {
    Tick tick{};
    std::uint64_t time_us{}; // End of scan: (tick + 1) * period.
    std::vector<std::uint8_t> inputs;
    std::vector<std::uint8_t> outputs;
    std::int64_t position_um{};
    std::optional<ConveyorSnapshot> conveyor;
    std::vector<TimerState> timers;
    std::vector<CounterState> counters{}; // Integer CV, indexed by coil; empty without CTU.
    bool operator==(const Snapshot&) const = default;
};
struct RunResult {
    std::vector<Snapshot> trace;
    std::vector<Transition> events;
    std::optional<SessionStatus> control;
    std::vector<CounterEvent> counter_events;
    bool operator==(const RunResult&) const = default;
};
class RunCancelled : public std::runtime_error {
public:
    RunCancelled() : std::runtime_error("Simulation cancelled") {}
};
// Value-only inputs: no Qt, VTK, wall clock, I/O or shared mutable state.
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
                                 const std::vector<PlcOutput>& previous_outputs);
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
    const std::vector<PlcOutput>& previous_outputs, std::vector<TimerState>& timers, std::uint64_t period_us);
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
    const std::vector<PlcOutput>& previous_outputs, std::vector<TimerState>& timers,
    std::vector<CounterState>& counters, std::uint64_t period_us);
void record_counter_changes(const Program& program, const std::vector<CounterState>& before,
    const std::vector<CounterState>& after, Tick tick, std::uint64_t time_us,
    std::vector<CounterEvent>& events, bool stopped = false);
std::int64_t advance_linear(std::int64_t position, std::int64_t maximum, std::int64_t distance);
void validate(const RunSpec& spec);
RunResult simulate(const RunSpec& spec, std::stop_token stop = {});
RunSpec demo_scenario();
}
