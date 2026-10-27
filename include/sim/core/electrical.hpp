#pragma once
#include <cstdint>
#include <string_view>

namespace sim::core {
enum class DigitalState : std::uint8_t { LOW, HIGH, UNDEFINED, FAULT };
constexpr bool valid(DigitalState value) { return value >= DigitalState::LOW && value <= DigitalState::FAULT; }
constexpr std::string_view name(DigitalState value) {
    switch (value) {
    case DigitalState::LOW: return "LOW";
    case DigitalState::HIGH: return "HIGH";
    case DigitalState::UNDEFINED: return "UNDEFINED";
    case DigitalState::FAULT: return "FAULT";
    }
    return "INVALID";
}
struct PowerSupply { DigitalState state{DigitalState::HIGH}; bool operator==(const PowerSupply&) const = default; };
struct OperatorCommand { DigitalState state{DigitalState::LOW}; bool operator==(const OperatorCommand&) const = default; };
struct PlcInput { DigitalState state{DigitalState::UNDEFINED}; bool operator==(const PlcInput&) const = default; };
struct PlcOutput { DigitalState state{DigitalState::LOW}; bool operator==(const PlcOutput&) const = default; };
struct DriveCommand { DigitalState state{DigitalState::LOW}; bool operator==(const DriveCommand&) const = default; };
DigitalState invert(DigitalState state);
DigitalState series(DigitalState a, DigitalState b);
DigitalState parallel(DigitalState a, DigitalState b);
DigitalState drive_state(PowerSupply supply, DriveCommand command);
}
