#include "conveyor_codec.hpp"
#include <array>
#include <string_view>

namespace sim::project::detail {
namespace {
using Json = nlohmann::json;
using namespace core;
constexpr std::array<std::string_view, 5> ports{"command", "sensor", "plc_input", "plc_output", "motor_command"};
constexpr std::array<std::string_view, 3> targets{"command", "motor_power", "sensor_power"};
template<class Enum, std::size_t N> Enum from_name(const Json& json, const std::array<std::string_view, N>& names) {
    const auto value = json.get<std::string>();
    for (std::size_t i = 0; i < names.size(); ++i) { if (value == names[i]) { return static_cast<Enum>(i); } }
    throw Error(ErrorCode::schema, "Unknown functional enum: " + value);
}
DigitalState state(const Json& json) {
    constexpr std::array<std::string_view, 4> names{"LOW", "HIGH", "UNDEFINED", "FAULT"};
    return from_name<DigitalState>(json, names);
}
const std::string& entity(Port port, const ConveyorIdentities& ids) {
    switch (port) {
    case Port::command: return ids.command_id;
    case Port::sensor: return ids.sensor_id;
    case Port::plc_input: case Port::plc_output: return ids.plc_id;
    case Port::motor_command: return ids.motor_id;
    }
    throw Error(ErrorCode::semantic, "Invalid functional port");
}
const std::string& event_entity(ExternalTarget target, const ConveyorIdentities& ids) {
    switch (target) {
    case ExternalTarget::command: return ids.command_id;
    case ExternalTarget::motor_power: return ids.motor_id;
    case ExternalTarget::sensor_power: return ids.sensor_id;
    }
    throw Error(ErrorCode::semantic, "Invalid event target");
}
Json encode_endpoint(const Endpoint& endpoint, const ConveyorIdentities& ids) {
    return {{"entity", entity(endpoint.port, ids)},
            {"port", ports.at(static_cast<std::size_t>(endpoint.port))}, {"index", endpoint.index}};
}
Endpoint decode_endpoint(const Json& json, const ConveyorIdentities& ids) {
    const auto port = from_name<Port>(json.at("port"), ports);
    if (json.at("entity") != entity(port, ids)) {
        throw Error(ErrorCode::semantic, "Connection entity reference does not match its functional port");
    }
    return {port, json.at("index").get<std::uint32_t>()};
}
}
nlohmann::json encode_conveyor(const Document& doc) {
    const auto& c = *doc.simulation.conveyor;
    const auto& ids = *doc.conveyor_ids;
    Json connections = Json::array();
    for (const auto& wire : c.connections) {
        connections.push_back({{"source", encode_endpoint(wire.source, ids)},
                               {"target", encode_endpoint(wire.target, ids)},
                               {"timing", wire.timing == Timing::same_scan ? "same_scan" : "previous_step"}});
    }
    Json events = Json::array();
    for (const auto& event : c.events) {
        events.push_back({{"tick", event.tick}, {"sequence", event.sequence},
                          {"target", targets.at(static_cast<std::size_t>(event.target))},
                          {"entity", event_entity(event.target, ids)}, {"value", name(event.value)}});
    }
    return {{"axis_ref", doc.axis_id}, {"plc_id", ids.plc_id}, {"motor_id", ids.motor_id},
            {"piece_id", ids.piece_id}, {"sensor_id", ids.sensor_id}, {"command_id", ids.command_id},
            {"speed_um_per_s", c.speed_um_per_s}, {"piece_length_um", c.piece_length_um},
            {"sensor_begin_um", c.sensor_begin_um}, {"sensor_end_um", c.sensor_end_um},
            {"initial_command", name(c.initial_command.state)}, {"motor_power", name(c.motor_power.state)},
            {"sensor_power", name(c.sensor_power.state)}, {"connections", std::move(connections)},
            {"events", std::move(events)}};
}
void decode_conveyor(const nlohmann::json& json, Document& doc) {
    if (json.at("axis_ref") != doc.axis_id) { throw Error(ErrorCode::semantic, "Unknown conveyor axis reference"); }
    ConveyorIdentities ids{json.at("plc_id").get<std::string>(), json.at("motor_id").get<std::string>(),
                            json.at("piece_id").get<std::string>(), json.at("sensor_id").get<std::string>(),
                            json.at("command_id").get<std::string>()};
    Conveyor c;
    // Schema and exact INT64 domain checks run before this conversion.
    c.speed_um_per_s = json.at("speed_um_per_s").get<std::int64_t>();
    c.piece_length_um = json.at("piece_length_um").get<std::int64_t>();
    c.sensor_begin_um = json.at("sensor_begin_um").get<std::int64_t>();
    c.sensor_end_um = json.at("sensor_end_um").get<std::int64_t>();
    c.initial_command.state = state(json.at("initial_command"));
    c.motor_power.state = state(json.at("motor_power"));
    c.sensor_power.state = state(json.at("sensor_power"));
    for (const auto& wire : json.at("connections")) {
        c.connections.push_back({decode_endpoint(wire.at("source"), ids), decode_endpoint(wire.at("target"), ids),
            wire.at("timing") == "same_scan" ? Timing::same_scan : Timing::previous_step});
    }
    for (const auto& event : json.at("events")) {
        const auto target = from_name<ExternalTarget>(event.at("target"), targets);
        if (event.at("entity") != event_entity(target, ids)) {
            throw Error(ErrorCode::semantic, "Functional event references the wrong entity");
        }
        c.events.push_back({event.at("tick").get<std::uint64_t>(), event.at("sequence").get<std::uint64_t>(),
                            target, state(event.at("value"))});
    }
    doc.conveyor_ids = std::move(ids);
    doc.simulation.conveyor = std::move(c);
}
}
