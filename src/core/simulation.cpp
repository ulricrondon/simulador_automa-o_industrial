#include "sim/core/simulation.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace sim::core {
namespace {
constexpr Tick max_ticks = 100'000;
constexpr std::uint32_t max_signals = 256;
constexpr std::size_t max_contacts = 4096;
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
}
void validate(const RunSpec& spec) {
    require(spec.ticks > 0 && spec.ticks <= max_ticks, "ticks must be in [1, 100000]");
    require(spec.tick_period_us > 0, "tick period must be positive");
    require(spec.tick_period_us <= std::numeric_limits<std::uint64_t>::max() / spec.ticks,
            "simulation duration overflows");
    const auto& p = spec.program;
    require(p.input_count > 0 && p.input_count <= max_signals, "invalid input count");
    require(p.output_count > 0 && p.output_count <= max_signals, "invalid output count");
    require(p.rungs.size() <= p.output_count, "too many rungs");
    std::set<Signal> coils;
    std::size_t contacts = 0;
    for (const auto& rung : p.rungs) {
        require(rung.coil < p.output_count, "coil out of range");
        require(coils.insert(rung.coil).second, "multiple writers for coil");
        require(!rung.branches.empty() && rung.branches.size() <= max_contacts, "invalid branches");
        for (const auto& branch : rung.branches) {
            require(!branch.empty() && branch.size() <= max_contacts, "invalid branch");
            contacts += branch.size();
            require(contacts <= max_contacts, "too many contacts");
            for (const auto& contact : branch) {
                require(contact.bank == Bank::input || contact.bank == Bank::output, "invalid signal bank");
                require(contact.signal < (contact.bank == Bank::input ? p.input_count : p.output_count),
                        "contact out of range");
            }
        }
    }
    require(spec.axis.drive < p.output_count, "axis drive out of range");
    require(spec.axis.maximum_um >= 0, "axis maximum must be nonnegative");
    require(spec.axis.initial_um >= 0 && spec.axis.initial_um <= spec.axis.maximum_um,
            "axis initial position out of range");
    require(spec.axis.travel_um_per_tick >= 0, "axis travel must be nonnegative");
    require(spec.events.size() <= 100'000, "too many events");
    std::set<std::uint64_t> sequences;
    for (const auto& event : spec.events) {
        require(event.tick < spec.ticks, "event outside run");
        require(event.signal < p.input_count, "event signal out of range");
        require(sequences.insert(event.sequence).second, "duplicate event sequence");
    }
}
RunResult simulate(const RunSpec& spec, std::stop_token stop) {
    if (stop.stop_requested()) { throw RunCancelled{}; }
    validate(spec);
    auto events = spec.events;
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return std::tie(a.tick, a.sequence) < std::tie(b.tick, b.sequence);
    });
    std::vector<std::uint8_t> inputs(spec.program.input_count, 0);
    std::vector<std::uint8_t> outputs(spec.program.output_count, 0);
    auto position = spec.axis.initial_um;
    RunResult result;
    result.trace.reserve(static_cast<std::size_t>(spec.ticks));
    std::size_t next_event = 0;
    for (Tick tick = 0; tick < spec.ticks; ++tick) {
        if (stop.stop_requested()) { throw RunCancelled{}; }
        while (next_event < events.size() && events[next_event].tick == tick) {
            const auto& event = events[next_event++];
            inputs[event.signal] = static_cast<std::uint8_t>(event.value);
        }
        // All rungs read the previous output image; rung order cannot change results.
        std::vector<std::uint8_t> next_outputs(spec.program.output_count, 0);
        for (const auto& rung : spec.program.rungs) {
            bool powered = false;
            for (const auto& branch : rung.branches) {
                bool closed = true;
                for (const auto& contact : branch) {
                    const bool value = (contact.bank == Bank::input ? inputs : outputs)[contact.signal] != 0;
                    closed = closed && (contact.normally_closed ? !value : value);
                }
                powered = powered || closed;
            }
            next_outputs[rung.coil] = static_cast<std::uint8_t>(powered);
        }
        outputs = std::move(next_outputs);
        if (outputs[spec.axis.drive] != 0) {
            // Difference first: avoid signed overflow near INT64_MAX.
            position += std::min(spec.axis.travel_um_per_tick, spec.axis.maximum_um - position);
        }
        result.trace.push_back({tick, (tick + 1) * spec.tick_period_us, inputs, outputs, position});
    }
    return result;
}
RunSpec demo_scenario() {
    RunSpec spec;
    spec.program.rungs = {Rung{{Branch{Contact{Bank::input, 0, false}}}, 0}};
    spec.ticks = 8;
    spec.axis.maximum_um = 25;
    spec.events = {{1, 0, 0, true}, {4, 1, 0, false}};
    return spec;
}
}
