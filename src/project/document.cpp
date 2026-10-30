#include "sim/project/document.hpp"
#include "project_schema.hpp"
#include "conveyor_codec.hpp"
#include "sim/core/conveyor.hpp"
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace sim::project {
namespace {
using Json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition) { throw Error(ErrorCode::semantic, message); }
}
template<class T> T number(const Json& value) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
        throw Error(ErrorCode::semantic, "Expected nonnegative integer");
    }
    const auto raw = value.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        throw Error(ErrorCode::semantic, "Integer exceeds destination range");
    }
    return static_cast<T>(raw);
}
class SchemaErrors : public nlohmann::json_schema::error_handler {
public:
    void error(const Json::json_pointer& pointer, const Json&, const std::string& message) override {
        throw Error(ErrorCode::schema, pointer.to_string() + ": " + message);
    }
};
void check_integer_domain(const Json& json) {
    // Validator 2.4.0 uses int64 for integer constraints. Reject before it can narrow uint64.
    if (json.is_number_unsigned() && json.get<std::uint64_t>() >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw Error(ErrorCode::semantic, "Project integers must not exceed INT64_MAX");
    }
    if (json.is_structured()) {
        for (const auto& child : json) { check_integer_domain(child); }
    }
}
void check_schema(const Json& json) {
    check_integer_domain(json);
    // Thread-local immutable schema: no shared validator state or external resolution.
    static thread_local nlohmann::json_schema::json_validator validator{
        Json::parse(detail::schema_text),
        [](const nlohmann::json_uri&, Json&) {
            throw Error(ErrorCode::schema, "External schema resolution is disabled");
        }};
    static thread_local nlohmann::json_schema::json_validator conveyor_validator{
        Json::parse(detail::schema_conveyor_text),
        [](const nlohmann::json_uri&, Json&) {
            throw Error(ErrorCode::schema, "External schema resolution is disabled");
        }};
    static thread_local nlohmann::json_schema::json_validator ton_validator{
        Json::parse(detail::schema_ton_text),
        [](const nlohmann::json_uri&, Json&) { throw Error(ErrorCode::schema, "External schema resolution is disabled"); }};
    static thread_local nlohmann::json_schema::json_validator ctu_validator{
        Json::parse(detail::schema_ctu_text),
        [](const nlohmann::json_uri&, Json&) { throw Error(ErrorCode::schema, "External schema resolution is disabled"); }};
    static thread_local nlohmann::json_schema::json_validator compare_validator{
        Json::parse(detail::schema_compare_text),
        [](const nlohmann::json_uri&, Json&) { throw Error(ErrorCode::schema, "External schema resolution is disabled"); }};
    SchemaErrors errors;
    if (json.value("engine", std::string{}) == "ladder-compare-v1") { compare_validator.validate(json, errors); return; }
    if (json.value("engine", std::string{}) == "ladder-ctu-v1") { ctu_validator.validate(json, errors); return; }
    if (json.value("engine", std::string{}) == "conveyor-ton-v1") { ton_validator.validate(json, errors); return; }
    if (json.value("engine", std::string{}) == "conveyor-v1") { conveyor_validator.validate(json, errors); }
    else { validator.validate(json, errors); }
}
void check_version(const Json& json) {
    if (!json.is_object() || !json.contains("format_version") ||
        !json["format_version"].is_object() || !json.contains("model_version") || !json.contains("engine")) {
        throw Error(ErrorCode::schema, "Missing version envelope");
    }
    const auto& version = json["format_version"];
    if (!version.contains("major") || !version.contains("minor")) {
        throw Error(ErrorCode::schema, "Missing format version fields");
    }
    const bool legacy = version["major"] == 1 && version["minor"] == 0 &&
                        json["model_version"] == 1 && json["engine"] == "discrete-v1";
    const bool conveyor = version["major"] == 1 && version["minor"] == 1 &&
                          json["model_version"] == 2 && json["engine"] == "conveyor-v1";
    const bool ton = version["major"] == 1 && version["minor"] == 2 && json["model_version"] == 3 && json["engine"] == "conveyor-ton-v1";
    const bool ctu = version["major"] == 1 && version["minor"] == 3 && json["model_version"] == 4 && json["engine"] == "ladder-ctu-v1";
    const bool compare = version["major"] == 1 && version["minor"] == 4 && json["model_version"] == 5 && json["engine"] == "ladder-compare-v1";
    if (!legacy && !conveyor && !ton && !ctu && !compare) {
        throw Error(ErrorCode::unsupported_version,
                    "Unsupported format/model/engine version; explicit migration required");
    }
}
void check_semantics(const Document& doc) {
    require(!core::has_timers(doc.simulation.program) || doc.simulation.conveyor.has_value() || core::has_counters(doc.simulation.program), "Persisted TON requires conveyor profile");
    std::set<std::string> ids;
    const auto register_id = [&ids](const std::string& id) {
        require(is_uuid_v4(id), "Expected canonical lowercase UUIDv4");
        require(ids.insert(id).second, "Duplicate entity UUID: " + id);
    };
    register_id(doc.project_id);
    register_id(doc.axis_id);
    require(doc.conveyor_ids.has_value() == doc.simulation.conveyor.has_value(), "Conveyor identities/model mismatch");
    if (doc.conveyor_ids) {
        const auto& ids = *doc.conveyor_ids;
        for (const auto* id : {&ids.plc_id, &ids.motor_id, &ids.piece_id, &ids.sensor_id, &ids.command_id}) {
            register_id(*id);
        }
    }
    require(doc.rung_ids.size() == doc.simulation.program.rungs.size(), "Rung identity count mismatch");
    require(doc.assets.size() <= 4096, "Too many assets");
    // Byte bound is additional to the schema's Unicode code-point limit.
    require(!doc.title.empty() && doc.title.size() <= 640, "Invalid title length");
    try { const auto encoded_title = Json(doc.title).dump(); (void)encoded_title; }
    catch (const Json::exception& error) { throw Error(ErrorCode::invalid_json, error.what()); }
    for (const auto& id : doc.rung_ids) { register_id(id); }
    std::map<std::string, std::pair<std::uint64_t, std::string>> catalog;
    for (const auto& asset : doc.assets) {
        register_id(asset.id);
        require(is_sha256(asset.sha256), "Invalid normalized SHA-256");
        require(asset.byte_size <= max_asset_bytes, "Asset exceeds size limit");
        require(asset.media_type == "model/gltf+json" || asset.media_type == "model/gltf-binary",
                "Unsupported asset media type");
        const auto [it, inserted] = catalog.emplace(asset.sha256, std::pair{asset.byte_size, asset.media_type});
        require(inserted || it->second == std::pair{asset.byte_size, asset.media_type},
                "Conflicting metadata for the same content digest");
    }
    try { core::validate(doc.simulation); }
    catch (const std::invalid_argument& error) { throw Error(ErrorCode::semantic, error.what()); }
}
Json encode(const Document& doc) {
    const auto& spec = doc.simulation;
    Json rungs = Json::array();
    for (std::size_t i = 0; i < spec.program.rungs.size(); ++i) {
        const auto& rung = spec.program.rungs[i];
        Json branches = Json::array();
        for (const auto& branch : rung.branches) {
            Json contacts = Json::array();
            for (const auto& contact : branch) {
                if (contact.comparison) {
                    const auto& c = *contact.comparison;
                    const auto it = std::find_if(spec.program.rungs.begin(), spec.program.rungs.end(),
                        [&](const auto& r) { return r.coil == c.counter.coil && r.ctu.has_value(); });
                    require(it != spec.program.rungs.end(), "Comparison target is not a CTU");
                    const auto index = static_cast<std::size_t>(std::distance(spec.program.rungs.begin(), it));
                    contacts.push_back({{"compare", {{"counter_ref", doc.rung_ids.at(index)},
                        {"operator", std::string(core::comparison_name(c.op))}, {"constant", c.constant}}}});
                    continue;
                }
                contacts.push_back({{"bank", contact.bank == core::Bank::input ? "input" : "output"},
                                    {"signal", contact.signal}, {"normally_closed", contact.normally_closed}});
            }
            branches.push_back(std::move(contacts));
        }
        rungs.push_back({{"id", doc.rung_ids.at(i)}, {"coil", rung.coil}, {"branches", std::move(branches)}});
        if (rung.ton_us) { rungs.back()["ton_us"] = *rung.ton_us; }
        if (rung.ctu) {
            const auto& r = rung.ctu->reset;
            rungs.back()["ctu"] = {{"pv", rung.ctu->pv},
                {"reset", {{"bank", r.bank == core::Bank::input ? "input" : "output"},
                           {"signal", r.signal}, {"normally_closed", r.normally_closed}}}};
        }
    }
    Json events = Json::array();
    for (const auto& event : spec.events) {
        events.push_back({{"tick", event.tick}, {"sequence", event.sequence},
                          {"signal", event.signal}, {"value", event.value}});
    }
    Json assets = Json::array();
    for (const auto& asset : doc.assets) {
        assets.push_back({{"id", asset.id}, {"sha256", asset.sha256},
                          {"byte_size", asset.byte_size}, {"media_type", asset.media_type}});
    }
    Json json = {
        {"format", "industrial-sim-project"}, {"format_version", {{"major", 1}, {"minor", 0}}},
        {"model_version", 1}, {"engine", "discrete-v1"}, {"project_id", doc.project_id}, {"title", doc.title},
        {"simulation", {
            {"ticks", spec.ticks}, {"tick_period_us", spec.tick_period_us},
            {"program", {{"input_count", spec.program.input_count}, {"output_count", spec.program.output_count},
                         {"rungs", std::move(rungs)}}},
            {"axis", {{"id", doc.axis_id}, {"drive", spec.axis.drive}, {"initial_um", spec.axis.initial_um},
                      {"maximum_um", spec.axis.maximum_um}, {"travel_um_per_tick", spec.axis.travel_um_per_tick}}},
            {"events", std::move(events)}}},
        {"assets", std::move(assets)}
    };
    if (spec.conveyor) {
        json["format_version"]["minor"] = 1;
        json["model_version"] = 2;
        json["engine"] = "conveyor-v1";
        json["simulation"]["conveyor"] = detail::encode_conveyor(doc);
    }
    if (core::has_timers(spec.program)) {
        json["format_version"]["minor"] = 2; json["model_version"] = 3; json["engine"] = "conveyor-ton-v1";
    }
    if (core::has_counters(spec.program)) {
        json["format_version"]["minor"] = 3; json["model_version"] = 4; json["engine"] = "ladder-ctu-v1";
    }
    if (core::has_comparisons(spec.program)) {
        json["format_version"]["minor"] = 4; json["model_version"] = 5; json["engine"] = "ladder-compare-v1";
    }
    return json;
}
Document decode(const Json& json) {
    Document doc;
    doc.project_id = json.at("project_id").get<std::string>();
    doc.title = json.at("title").get<std::string>();
    const auto& simulation = json.at("simulation");
    auto& spec = doc.simulation;
    spec.ticks = number<core::Tick>(simulation.at("ticks"));
    spec.tick_period_us = number<std::uint64_t>(simulation.at("tick_period_us"));
    const auto& program = simulation.at("program");
    spec.program.input_count = number<std::uint32_t>(program.at("input_count"));
    spec.program.output_count = number<std::uint32_t>(program.at("output_count"));
    for (const auto& rung_json : program.at("rungs")) {
        doc.rung_ids.push_back(rung_json.at("id").get<std::string>());
        core::Rung rung;
        rung.coil = number<core::Signal>(rung_json.at("coil"));
        if (rung_json.contains("ton_us")) { rung.ton_us = number<std::uint64_t>(rung_json.at("ton_us")); }
        if (rung_json.contains("ctu")) {
            const auto& c = rung_json.at("ctu");
            const auto& r = c.at("reset");
            rung.ctu = core::CounterConfig{number<std::int64_t>(c.at("pv")),
                {r.at("bank") == "input" ? core::Bank::input : core::Bank::output,
                 number<core::Signal>(r.at("signal")), r.at("normally_closed").get<bool>()}};
        }
        for (const auto& branch_json : rung_json.at("branches")) {
            core::Branch branch;
            for (const auto& contact : branch_json) {
                if (contact.contains("compare")) {
                    const auto& c = contact.at("compare");
                    std::optional<core::Signal> target;
                    for (const auto& candidate : program.at("rungs")) {
                        if (candidate.at("id") == c.at("counter_ref")) {
                            require(!target && candidate.contains("ctu"), "Ambiguous or incompatible CTU reference");
                            target = number<core::Signal>(candidate.at("coil"));
                        }
                    }
                    require(target.has_value(), "Unknown CTU reference");
                    const auto op_text = c.at("operator").get<std::string>();
                    std::optional<core::ComparisonOp> op;
                    for (auto candidate : {core::ComparisonOp::equal, core::ComparisonOp::not_equal,
                        core::ComparisonOp::less, core::ComparisonOp::less_equal, core::ComparisonOp::greater,
                        core::ComparisonOp::greater_equal}) {
                        if (core::comparison_name(candidate) == op_text) { op = candidate; }
                    }
                    require(op.has_value(), "Unknown comparison operator");
                    // check_integer_domain/schema/parser already reject overflow and non-integers.
                    require(c.at("constant").is_number_integer(), "Comparison requires an int64 constant");
                    branch.push_back(core::Contact::compare({{*target}, *op, c.at("constant").get<std::int64_t>()}));
                    continue;
                }
                branch.push_back({contact.at("bank") == "input" ? core::Bank::input : core::Bank::output,
                                  number<core::Signal>(contact.at("signal")),
                                  contact.at("normally_closed").get<bool>()});
            }
            rung.branches.push_back(std::move(branch));
        }
        spec.program.rungs.push_back(std::move(rung));
    }
    const auto& axis = simulation.at("axis");
    doc.axis_id = axis.at("id").get<std::string>();
    spec.axis = {number<core::Signal>(axis.at("drive")), number<std::int64_t>(axis.at("initial_um")),
                 number<std::int64_t>(axis.at("maximum_um")), number<std::int64_t>(axis.at("travel_um_per_tick"))};
    for (const auto& event : simulation.at("events")) {
        spec.events.push_back({number<core::Tick>(event.at("tick")), number<std::uint64_t>(event.at("sequence")),
                               number<core::Signal>(event.at("signal")), event.at("value").get<bool>()});
    }
    if (simulation.contains("conveyor")) { detail::decode_conveyor(simulation.at("conveyor"), doc); }
    for (const auto& asset : json.at("assets")) {
        doc.assets.push_back({asset.at("id").get<std::string>(), asset.at("sha256").get<std::string>(),
                              number<std::uint64_t>(asset.at("byte_size")), asset.at("media_type").get<std::string>()});
    }
    return doc;
}
}
Document parse(std::string_view utf8) {
    if (utf8.size() > max_document_bytes) { throw Error(ErrorCode::limit, "Project JSON exceeds 16 MiB"); }
    try {
        std::vector<std::set<std::string>> object_keys;
        const auto callback = [&object_keys](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 64) { throw Error(ErrorCode::limit, "Project JSON nesting exceeds 64"); }
            if (event == Json::parse_event_t::object_start) { object_keys.emplace_back(); }
            if (event == Json::parse_event_t::key &&
                !object_keys.back().insert(value.get<std::string>()).second) {
                throw Error(ErrorCode::invalid_json, "Duplicate JSON object key");
            }
            if (event == Json::parse_event_t::object_end) { object_keys.pop_back(); }
            if (event == Json::parse_event_t::value && value.is_number_float()) {
                throw Error(ErrorCode::invalid_json, "Project format requires exact integer numbers");
            }
            return true;
        };
        const auto json = Json::parse(utf8.begin(), utf8.end(), callback);
        check_version(json);
        check_schema(json);
        auto doc = decode(json);
        check_semantics(doc);
        return doc;
    } catch (const Json::exception& error) {
        throw Error(ErrorCode::invalid_json, error.what());
    }
}
void validate(const Document& document) {
    check_semantics(document);
    check_schema(encode(document));
}
std::string serialize(const Document& document) {
    try {
        check_semantics(document);
        const auto json = encode(document);
        check_schema(json);
        auto text = json.dump(2) + "\n";
        if (text.size() > max_document_bytes) { throw Error(ErrorCode::limit, "Serialized project exceeds 16 MiB"); }
        return text;
    } catch (const Json::exception& error) {
        throw Error(ErrorCode::invalid_json, error.what());
    }
}
Document load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { throw Error(ErrorCode::io, "Cannot open project file"); }
    std::string text;
    std::array<char, 8192> chunk{};
    while (file) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > max_document_bytes - text.size()) { throw Error(ErrorCode::limit, "Project file exceeds 16 MiB"); }
        text.append(chunk.data(), count);
    }
    if (!file.eof()) { throw Error(ErrorCode::io, "Cannot read project file"); }
    return parse(text);
}
Document new_demo(std::string title) {
    Document doc;
    doc.project_id = generate_uuid_v4();
    doc.axis_id = generate_uuid_v4();
    doc.title = std::move(title);
    doc.simulation = core::demo_scenario();
    for (std::size_t i = 0; i < doc.simulation.program.rungs.size(); ++i) {
        doc.rung_ids.push_back(generate_uuid_v4());
    }
    validate(doc);
    return doc;
}
Document new_conveyor_demo(std::string title) {
    auto doc = new_demo(std::move(title));
    doc.simulation = core::conveyor_scenario();
    doc.conveyor_ids = ConveyorIdentities{generate_uuid_v4(), generate_uuid_v4(), generate_uuid_v4(),
                                           generate_uuid_v4(), generate_uuid_v4()};
    validate(doc);
    return doc;
}
}
