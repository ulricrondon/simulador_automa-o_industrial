#pragma once
#include "sim/core/electrical.hpp"
#include <cstdint>
#include <vector>

namespace sim::core {
enum class SessionState { running, paused, stopped, completed };
enum class ControlAction { step, pause, resume, stop };
struct SessionStatus {
    SessionState state{};
    std::uint64_t next_tick{};
    std::int64_t position_um{};
    DigitalState motor{};
    bool operator==(const SessionStatus&) const = default;
};
enum class Port { command, sensor, plc_input, plc_output, motor_command };
enum class Timing { same_scan, previous_step };
struct Endpoint {
    Port port{Port::command};
    std::uint32_t index{};
    bool operator==(const Endpoint&) const = default;
};
struct Connection {
    Endpoint source;
    Endpoint target;
    Timing timing{Timing::same_scan};
    bool operator==(const Connection&) const = default;
};
enum class ExternalTarget { command, motor_power, sensor_power };
struct FunctionalEvent {
    std::uint64_t tick{};
    std::uint64_t sequence{};
    ExternalTarget target{ExternalTarget::command};
    DigitalState value{DigitalState::LOW};
    bool operator==(const FunctionalEvent&) const = default;
};
struct Conveyor {
    std::int64_t speed_um_per_s{10000};
    std::int64_t piece_length_um{10};
    std::int64_t sensor_begin_um{25};
    std::int64_t sensor_end_um{30};
    OperatorCommand initial_command;
    PowerSupply motor_power;
    PowerSupply sensor_power;
    std::vector<Connection> connections;
    std::vector<FunctionalEvent> events;
    bool operator==(const Conveyor&) const = default;
};
struct ConveyorSnapshot {
    OperatorCommand command;
    PowerSupply motor_power;
    PowerSupply sensor_power;
    std::vector<PlcInput> plc_inputs;
    std::vector<PlcOutput> plc_outputs;
    DriveCommand motor_command;
    DigitalState motor_state{DigitalState::LOW};
    DigitalState sampled_sensor{DigitalState::LOW};
    DigitalState sensor_state{DigitalState::LOW};
    std::int64_t velocity_um_per_s{};
    std::int64_t fractional_um_millionths{};
    bool operator==(const ConveyorSnapshot&) const = default;
};
enum class TransitionKind {
    command, motor_power, sensor_power, plc_input, plc_output, motor,
    sensor, piece, paused, resumed, stopped
};
enum class Phase { external, sample, ladder, drive, mechanics, sensor, control };
struct Transition {
    std::uint64_t tick{};
    std::uint64_t time_us{};
    Phase phase{Phase::external};
    TransitionKind kind{TransitionKind::command};
    std::uint32_t signal{};
    DigitalState state{DigitalState::LOW}; // Used by electrical changes.
    std::int64_t position_um{}; // Used by piece changes.
    bool operator==(const Transition&) const = default;
};
}
