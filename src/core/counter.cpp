#include "sim/core/simulation.hpp"
#include <algorithm>
#include <limits>
namespace sim::core {
bool has_counters(const Program& program) {
    return std::any_of(program.rungs.begin(), program.rungs.end(), [](const auto& r) { return r.ctu.has_value(); });
}
CounterState update_ctu(CounterState state, DigitalState cu, DigitalState reset, std::int64_t pv) {
    if (pv < 0 || state.cv < 0 || !valid(cu) || !valid(reset)) {
        throw std::invalid_argument("Invalid CTU state, preset or signal");
    }
    // Known reset wins over counting and diagnostics; PV=0 is explicitly inhibited during reset.
    if (reset == DigitalState::HIGH) { return {}; }
    if (reset != DigitalState::LOW || (cu != DigitalState::LOW && cu != DigitalState::HIGH)) {
        state.armed = false;
        state.q = reset == DigitalState::FAULT || cu == DigitalState::FAULT
            ? DigitalState::FAULT : DigitalState::UNDEFINED;
        return state; // Hold CV; no assumption that an unknown reset is off.
    }
    if (cu == DigitalState::HIGH && state.armed && state.cv < std::numeric_limits<std::int64_t>::max()) {
        ++state.cv;
    }
    state.armed = cu == DigitalState::LOW;
    state.q = state.cv >= pv ? DigitalState::HIGH : DigitalState::LOW;
    return state;
}
void record_counter_changes(const Program& program, const std::vector<CounterState>& before,
    const std::vector<CounterState>& after, Tick tick, std::uint64_t time_us,
    std::vector<CounterEvent>& events, bool stopped) {
    // Coil order, independent of rung storage order.
    for (Signal coil = 0; coil < program.output_count; ++coil) {
        if (before.at(coil) != after.at(coil)) {
            events.push_back({tick, time_us, coil, after[coil], stopped});
        }
    }
}
}
