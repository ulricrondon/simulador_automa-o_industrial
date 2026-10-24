#pragma once
#include <cstdint>
#include <stop_token>
#include <stdexcept>
#include <vector>

namespace sim::core {
using Tick = std::uint64_t;
using Signal = std::uint32_t;
enum class Bank { input, output };
struct Contact {
    Bank bank{Bank::input};
    Signal signal{};
    bool normally_closed{};
};
using Branch = std::vector<Contact>; // Series contacts: AND.
struct Rung {
    std::vector<Branch> branches; // Parallel branches: OR.
    Signal coil{};
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
};
struct Snapshot {
    Tick tick{};
    std::uint64_t time_us{}; // End of scan: (tick + 1) * period.
    std::vector<std::uint8_t> inputs;
    std::vector<std::uint8_t> outputs;
    std::int64_t position_um{};
    bool operator==(const Snapshot&) const = default;
};
struct RunResult {
    std::vector<Snapshot> trace;
    bool operator==(const RunResult&) const = default;
};
class RunCancelled : public std::runtime_error {
public:
    RunCancelled() : std::runtime_error("Simulation cancelled") {}
};
// Value-only inputs: no Qt, VTK, wall clock, I/O or shared mutable state.
void validate(const RunSpec& spec);
RunResult simulate(const RunSpec& spec, std::stop_token stop = {});
RunSpec demo_scenario();
}
