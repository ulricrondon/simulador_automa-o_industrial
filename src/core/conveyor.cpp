#include "sim/core/conveyor.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace sim::core {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::invalid_argument(message); }
}
DigitalState detect(const Conveyor& model, PowerSupply power, std::int64_t from, std::int64_t to) {
    if (power.state == DigitalState::FAULT) { return DigitalState::FAULT; }
    if (power.state != DigitalState::HIGH) { return DigitalState::UNDEFINED; }
    // Forward swept piece interval [from, to + length) vs sensor [begin, end).
    return from < model.sensor_end_um && to + model.piece_length_um > model.sensor_begin_um
        ? DigitalState::HIGH : DigitalState::LOW;
}
}
void validate_conveyor(const RunSpec& spec) {
    require(spec.conveyor.has_value() && spec.tick_period_us > 0, "Conveyor model and positive timestep required");
    const auto& c = *spec.conveyor;
    require(spec.events.empty(), "Conveyor inputs have typed owners; legacy input events are not allowed");
    require(spec.axis.travel_um_per_tick == 0, "Conveyor uses speed, not legacy travel per tick");
    require(c.piece_length_um > 0 && c.piece_length_um <= spec.axis.maximum_um, "Invalid piece length");
    require(spec.axis.initial_um <= spec.axis.maximum_um - c.piece_length_um, "Piece outside conveyor");
    require(c.sensor_begin_um >= 0 && c.sensor_end_um > c.sensor_begin_um &&
            c.sensor_end_um <= spec.axis.maximum_um, "Invalid photoelectric zone");
    require(c.speed_um_per_s > 0, "Conveyor speed must be positive");
    require(static_cast<std::uint64_t>(c.speed_um_per_s) <=
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - 999999) / spec.tick_period_us,
            "Conveyor speed and timestep overflow fixed-point displacement");
    require(valid(c.initial_command.state) && valid(c.motor_power.state) && valid(c.sensor_power.state),
            "Invalid initial electrical state");
    require(c.connections.size() == 3, "Conveyor requires three functional connections");
    std::set<Signal> input_writers;
    int command_links = 0, sensor_links = 0, drive_links = 0;
    for (const auto& wire : c.connections) {
        if (wire.source.port == Port::command || wire.source.port == Port::sensor) {
            require(wire.source.index == 0 && wire.target.port == Port::plc_input &&
                    wire.target.index < spec.program.input_count, "Invalid PLC input connection");
            require(input_writers.insert(wire.target.index).second, "Multiple drivers for PLC input");
            if (wire.source.port == Port::sensor) {
                require(wire.timing == Timing::previous_step,
                        "Instantaneous sensor feedback would create an undefined algebraic cycle");
                ++sensor_links;
            } else {
                require(wire.timing == Timing::same_scan, "Command must be sampled in the current scan");
                ++command_links;
            }
        } else {
            require(wire.source.port == Port::plc_output && wire.source.index < spec.program.output_count &&
                    wire.target.port == Port::motor_command && wire.target.index == 0 &&
                    wire.timing == Timing::same_scan, "Invalid motor command connection");
            require(wire.source.index == spec.axis.drive, "Motor connection must match axis drive");
            require(std::any_of(spec.program.rungs.begin(), spec.program.rungs.end(),
                               [&](const auto& rung) { return rung.coil == wire.source.index; }),
                    "Motor command has no Ladder coil writer");
            ++drive_links;
        }
    }
    require(command_links == 1 && sensor_links == 1 && drive_links == 1, "Incomplete functional wiring");
    require(c.events.size() <= 100000, "Too many functional events");
    std::set<std::uint64_t> sequences;
    for (const auto& event : c.events) {
        require(event.tick < spec.ticks && valid(event.value), "Invalid functional event");
        require(event.target == ExternalTarget::command || event.target == ExternalTarget::motor_power ||
                event.target == ExternalTarget::sensor_power, "Invalid event target");
        require(sequences.insert(event.sequence).second, "Duplicate functional event sequence");
    }
}
ConveyorSession::ConveyorSession(RunSpec spec) : spec_(std::move(spec)) {
    require(spec_.conveyor.has_value(), "Conveyor session requires a conveyor model");
    validate(spec_);
    auto& c = *spec_.conveyor;
    std::sort(c.events.begin(), c.events.end(), [](const auto& a, const auto& b) {
        return std::tie(a.tick, a.sequence) < std::tie(b.tick, b.sequence);
    });
    position_ = spec_.axis.initial_um;
    live_.command = c.initial_command;
    live_.motor_power = c.motor_power;
    live_.sensor_power = c.sensor_power;
    live_.plc_inputs.resize(spec_.program.input_count);
    live_.plc_outputs.resize(spec_.program.output_count);
    timers_.resize(spec_.program.output_count);
    counters_.resize(spec_.program.output_count);
    live_.sensor_state = detect(c, live_.sensor_power, position_, position_);
    live_.sampled_sensor = live_.sensor_state;
    result_.trace.reserve(static_cast<std::size_t>(spec_.ticks));
}
void ConveyorSession::record(Phase phase, TransitionKind kind, std::uint32_t index,
                             DigitalState value, bool end_of_step) {
    result_.events.push_back({tick_, (tick_ + (end_of_step ? 1 : 0)) * spec_.tick_period_us,
                              phase, kind, index, value, position_});
}
bool ConveyorSession::step() {
    if (state_ != SessionState::running) { return false; }
    const auto& c = *spec_.conveyor;
    // 1. External events at the start boundary, ordered by (tick, sequence).
    while (next_event_ < c.events.size() && c.events[next_event_].tick == tick_) {
        const auto& event = c.events[next_event_++];
        auto* destination = &live_.command.state;
        auto kind = TransitionKind::command;
        if (event.target == ExternalTarget::motor_power) {
            destination = &live_.motor_power.state; kind = TransitionKind::motor_power;
        } else if (event.target == ExternalTarget::sensor_power) {
            destination = &live_.sensor_power.state; kind = TransitionKind::sensor_power;
        }
        if (*destination != event.value) {
            *destination = event.value;
            record(Phase::external, kind, 0, event.value);
        }
    }
    // 2. One coherent PLC input image. Geometry feedback comes from the prior step.
    live_.sampled_sensor = live_.sensor_state;
    auto inputs = std::vector<PlcInput>(spec_.program.input_count);
    for (const auto& wire : c.connections) {
        if (wire.target.port == Port::plc_input) {
            inputs[wire.target.index].state = wire.source.port == Port::command
                ? live_.command.state : live_.sampled_sensor;
        }
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i] != live_.plc_inputs[i]) {
            record(Phase::sample, TransitionKind::plc_input, static_cast<Signal>(i), inputs[i].state);
        }
    }
    live_.plc_inputs = std::move(inputs);
    // 3. All rungs read previous output image, then publish coils together.
    const auto previous_counters = counters_;
    auto outputs = scan_ladder(spec_.program, live_.plc_inputs, live_.plc_outputs, timers_, counters_, spec_.tick_period_us);
    record_counter_changes(spec_.program, previous_counters, counters_, tick_, tick_ * spec_.tick_period_us, result_.counter_events);
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (outputs[i] != live_.plc_outputs[i]) {
            record(Phase::ladder, TransitionKind::plc_output, static_cast<Signal>(i), outputs[i].state);
        }
    }
    live_.plc_outputs = std::move(outputs);
    // 4. Command and power are distinct typed inputs to the drive.
    live_.motor_command = DriveCommand{live_.plc_outputs[spec_.axis.drive].state};
    const auto motor = drive_state(live_.motor_power, live_.motor_command);
    if (motor != live_.motor_state) { record(Phase::drive, TransitionKind::motor, 0, motor); }
    live_.motor_state = motor;
    live_.velocity_um_per_s = motor == DigitalState::HIGH ? c.speed_um_per_s : 0;
    // 5. Integer integration with sub-micrometer remainder; no wall-clock delta.
    const auto old_position = position_;
    const auto limit = spec_.axis.maximum_um - c.piece_length_um;
    if (motor == DigitalState::HIGH && position_ < limit) {
        const auto numerator = c.speed_um_per_s * static_cast<std::int64_t>(spec_.tick_period_us) + remainder_;
        position_ = advance_linear(position_, limit, numerator / 1000000);
        remainder_ = position_ == limit ? 0 : numerator % 1000000;
    }
    if (position_ == limit) { live_.velocity_um_per_s = 0; }
    live_.fractional_um_millionths = remainder_;
    if (position_ != old_position) { record(Phase::mechanics, TransitionKind::piece, 0, DigitalState::LOW, true); }
    // 6. Sensor publication after mechanics. This cannot affect this scan's Ladder.
    const auto sensor = detect(c, live_.sensor_power, old_position, position_);
    if (sensor != live_.sensor_state) { record(Phase::sensor, TransitionKind::sensor, 0, sensor, true); }
    live_.sensor_state = sensor;
    Snapshot snapshot;
    snapshot.tick = tick_;
    snapshot.time_us = (tick_ + 1) * spec_.tick_period_us;
    snapshot.position_um = position_;
    // Existing low/high clients retain their byte images; conveyor clients use typed data.
    for (const auto& input : live_.plc_inputs) { snapshot.inputs.push_back(static_cast<std::uint8_t>(input.state)); }
    for (const auto& output : live_.plc_outputs) { snapshot.outputs.push_back(static_cast<std::uint8_t>(output.state)); }
    snapshot.conveyor = live_;
    if (has_timers(spec_.program)) { snapshot.timers = timers_; }
    if (has_counters(spec_.program)) { snapshot.counters = counters_; }
    result_.trace.push_back(std::move(snapshot));
    ++tick_;
    if (tick_ == spec_.ticks) { state_ = SessionState::completed; }
    return true;
}
void ConveyorSession::pause() {
    if (state_ == SessionState::paused) { return; }
    if (state_ != SessionState::running) { throw std::logic_error("Cannot pause a terminal session"); }
    state_ = SessionState::paused;
    record(Phase::control, TransitionKind::paused, 0, DigitalState::LOW);
}
void ConveyorSession::resume() {
    if (state_ == SessionState::running) { return; }
    if (state_ != SessionState::paused) { throw std::logic_error("Only a paused session can resume"); }
    state_ = SessionState::running;
    record(Phase::control, TransitionKind::resumed, 0, DigitalState::LOW);
}
void ConveyorSession::stop() {
    if (state_ == SessionState::stopped || state_ == SessionState::completed) { return; }
    for (std::size_t i = 0; i < live_.plc_outputs.size(); ++i) {
        if (live_.plc_outputs[i].state != DigitalState::LOW) {
            live_.plc_outputs[i].state = DigitalState::LOW;
            record(Phase::control, TransitionKind::plc_output, static_cast<Signal>(i), DigitalState::LOW);
        }
    }
    const auto previous_counters = counters_;
    std::fill(counters_.begin(), counters_.end(), CounterState{});
    record_counter_changes(spec_.program, previous_counters, counters_, tick_, tick_ * spec_.tick_period_us, result_.counter_events, true);
    std::fill(timers_.begin(), timers_.end(), TimerState{});
    live_.motor_command.state = DigitalState::LOW;
    const auto motor = drive_state(live_.motor_power, live_.motor_command);
    if (motor != live_.motor_state) { record(Phase::control, TransitionKind::motor, 0, motor); }
    live_.motor_state = motor;
    live_.velocity_um_per_s = 0;
    state_ = SessionState::stopped;
    record(Phase::control, TransitionKind::stopped, 0, DigitalState::LOW);
}
RunResult simulate_conveyor(const RunSpec& spec, std::stop_token stop) {
    ConveyorSession session(spec);
    while (session.state() == SessionState::running) {
        if (stop.stop_requested()) { throw RunCancelled{}; }
        session.step();
    }
    return session.result();
}
RunSpec conveyor_scenario() {
    RunSpec spec;
    spec.ticks = 8;
    spec.program.input_count = 2;
    spec.program.rungs = {Rung{{Branch{{Bank::input, 0, false}, {Bank::input, 1, true}}}, 0}};
    spec.axis = {0, 0, 100, 0};
    Conveyor c;
    c.connections = {
        {{Port::command, 0}, {Port::plc_input, 0}, Timing::same_scan},
        {{Port::sensor, 0}, {Port::plc_input, 1}, Timing::previous_step},
        {{Port::plc_output, 0}, {Port::motor_command, 0}, Timing::same_scan}
    };
    c.events = {{1, 0, ExternalTarget::command, DigitalState::HIGH}};
    spec.conveyor = std::move(c);
    return spec;
}
}
