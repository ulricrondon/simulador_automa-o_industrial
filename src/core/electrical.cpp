#include "sim/core/simulation.hpp"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace sim::core {
namespace {
void check(DigitalState state) {
    if (!valid(state)) { throw std::invalid_argument("Invalid digital state"); }
}
}
DigitalState invert(DigitalState state) {
    check(state);
    if (state == DigitalState::LOW) { return DigitalState::HIGH; }
    if (state == DigitalState::HIGH) { return DigitalState::LOW; }
    return state;
}
DigitalState series(DigitalState a, DigitalState b) {
    check(a); check(b);
    if (a == DigitalState::FAULT || b == DigitalState::FAULT) { return DigitalState::FAULT; }
    if (a == DigitalState::LOW || b == DigitalState::LOW) { return DigitalState::LOW; }
    if (a == DigitalState::UNDEFINED || b == DigitalState::UNDEFINED) { return DigitalState::UNDEFINED; }
    return DigitalState::HIGH;
}
DigitalState parallel(DigitalState a, DigitalState b) {
    check(a); check(b);
    if (a == DigitalState::FAULT || b == DigitalState::FAULT) { return DigitalState::FAULT; }
    if (a == DigitalState::HIGH || b == DigitalState::HIGH) { return DigitalState::HIGH; }
    if (a == DigitalState::UNDEFINED || b == DigitalState::UNDEFINED) { return DigitalState::UNDEFINED; }
    return DigitalState::LOW;
}
DigitalState drive_state(PowerSupply supply, DriveCommand command) {
    return series(supply.state, command.state);
}
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
                                 const std::vector<PlcOutput>& previous_outputs) {
    if (has_timers(program) || has_counters(program)) { throw std::invalid_argument("Timed Ladder requires persistent timer state"); }
    std::vector<TimerState> timers(program.output_count);
    return scan_ladder(program, inputs, previous_outputs, timers, 1);
}
bool has_timers(const Program& program) {
    return std::any_of(program.rungs.begin(), program.rungs.end(), [](const auto& r) { return r.ton_us.has_value(); });
}
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
    const std::vector<PlcOutput>& previous_outputs, std::vector<TimerState>& timers, std::uint64_t period_us) {
    if (has_counters(program)) { throw std::invalid_argument("CTU requires persistent counter state"); }
    std::vector<CounterState> counters(program.output_count);
    return scan_ladder(program, inputs, previous_outputs, timers, counters, period_us);
}
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
    const std::vector<PlcOutput>& previous_outputs, std::vector<TimerState>& timers,
    std::vector<CounterState>& counters, std::uint64_t period_us) {
    validate_comparisons(program);
    // Reject duplicate instances before any state mutation, including direct scanner calls.
    std::set<Signal> writers;
    for (const auto& rung : program.rungs) {
        if (rung.coil >= program.output_count || !writers.insert(rung.coil).second ||
            (rung.ctu && (rung.ton_us || rung.ctu->pv < 0))) {
            throw std::invalid_argument("Invalid or duplicate stateful rung");
        }
    }
    if (counters.size() != program.output_count) { throw std::invalid_argument("Counter image size mismatch"); }
    if (period_us == 0 || timers.size() != program.output_count) {
        throw std::invalid_argument("Invalid timer state or period");
    }
    if (inputs.size() != program.input_count || previous_outputs.size() != program.output_count) {
        throw std::invalid_argument("PLC image size mismatch");
    }
    const auto previous_counters = counters; // Coherent read-only image for every rung.
    std::vector<PlcOutput> outputs(program.output_count);
    for (const auto& rung : program.rungs) {
        auto powered = DigitalState::LOW;
        for (const auto& branch : rung.branches) {
            auto closed = DigitalState::HIGH;
            for (const auto& contact : branch) {
                if (contact.comparison) {
                    closed = series(closed, evaluate_comparison(previous_counters.at(contact.comparison->counter.coil), *contact.comparison));
                    continue;
                }
                if (contact.bank != Bank::input && contact.bank != Bank::output) {
                    throw std::invalid_argument("Invalid PLC bank");
                }
                auto value = contact.bank == Bank::input ? inputs.at(contact.signal).state
                                                        : previous_outputs.at(contact.signal).state;
                closed = series(closed, contact.normally_closed ? invert(value) : value);
            }
            powered = parallel(powered, closed);
        }
        if (rung.ton_us) {
            auto& timer = timers.at(rung.coil);
            const auto preset = *rung.ton_us;
            if (powered != DigitalState::HIGH) { timer = {0, false, powered}; }
            else {
                if (!timer.active) { timer.elapsed_us = 0; timer.active = true; }
                else { timer.elapsed_us += std::min(period_us, preset - std::min(preset, timer.elapsed_us)); }
                timer.q = timer.elapsed_us >= preset ? DigitalState::HIGH : DigitalState::LOW;
            }
            powered = timer.q;
        }
        if (rung.ctu) {
            const auto& r = rung.ctu->reset;
            if (r.bank != Bank::input && r.bank != Bank::output) { throw std::invalid_argument("Invalid reset bank"); }
            auto reset = r.bank == Bank::input ? inputs.at(r.signal).state : previous_outputs.at(r.signal).state;
            if (r.normally_closed) { reset = invert(reset); }
            auto& counter = counters.at(rung.coil);
            counter = update_ctu(counter, powered, reset, rung.ctu->pv);
            powered = counter.q;
        }
        outputs.at(rung.coil).state = powered;
    }
    return outputs;
}
}
