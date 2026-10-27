#include "sim/core/simulation.hpp"
#include <stdexcept>
#include <algorithm>

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
    if (has_timers(program)) { throw std::invalid_argument("Timed Ladder requires persistent timer state"); }
    std::vector<TimerState> timers(program.output_count);
    return scan_ladder(program, inputs, previous_outputs, timers, 1);
}
bool has_timers(const Program& program) {
    return std::any_of(program.rungs.begin(), program.rungs.end(), [](const auto& r) { return r.ton_us.has_value(); });
}
std::vector<PlcOutput> scan_ladder(const Program& program, const std::vector<PlcInput>& inputs,
    const std::vector<PlcOutput>& previous_outputs, std::vector<TimerState>& timers, std::uint64_t period_us) {
    if (period_us == 0 || timers.size() != program.output_count) {
        throw std::invalid_argument("Invalid timer state or period");
    }
    if (inputs.size() != program.input_count || previous_outputs.size() != program.output_count) {
        throw std::invalid_argument("PLC image size mismatch");
    }
    std::vector<PlcOutput> outputs(program.output_count);
    for (const auto& rung : program.rungs) {
        auto powered = DigitalState::LOW;
        for (const auto& branch : rung.branches) {
            auto closed = DigitalState::HIGH;
            for (const auto& contact : branch) {
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
        outputs.at(rung.coil).state = powered;
    }
    return outputs;
}
}
