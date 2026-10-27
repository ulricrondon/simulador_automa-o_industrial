#include "sim/project/document.hpp"
#include "sim/core/conveyor.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace {
using namespace sim::project;
using Json=nlohmann::json;
const auto example=std::filesystem::path(SIM_FIXTURES_DIR).parent_path().parent_path()/"examples/conveyor.json";
Json fixture() { std::ifstream file(example); return Json::parse(file); }
TEST(ConveyorProject, ExistingFormatStaysUnchangedAndNewProfileIsExplicit) {
    const auto old=load(std::filesystem::path(SIM_FIXTURES_DIR)/"project-v1.json");
    const auto json=Json::parse(serialize(old));
    EXPECT_EQ(json["format_version"]["minor"],0);
    EXPECT_EQ(json["engine"],"discrete-v1");
    EXPECT_FALSE(json["simulation"].contains("conveyor"));
    const auto current=load(example);
    const auto encoded=Json::parse(serialize(current));
    EXPECT_EQ(encoded["format_version"]["minor"],1);
    EXPECT_EQ(encoded["model_version"],2);
    EXPECT_EQ(encoded["engine"],"conveyor-v1");
}
TEST(ConveyorProject, RoundTripPreservesConfigurationIdentitiesReferencesAndEvents) {
    const auto doc=load(example);
    const auto restored=parse(serialize(doc));
    EXPECT_EQ(restored.conveyor_ids,doc.conveyor_ids);
    EXPECT_EQ(restored.axis_id,doc.axis_id);
    EXPECT_EQ(restored.rung_ids,doc.rung_ids);
    EXPECT_EQ(restored.simulation.conveyor,doc.simulation.conveyor);
    EXPECT_EQ(serialize(restored),serialize(doc));
    EXPECT_EQ(sim::core::simulate(restored.simulation),sim::core::simulate(sim::core::conveyor_scenario()));
}
TEST(ConveyorProject, NewScenarioReusesDocumentFactoryWithFreshIdentities) {
    const auto a=new_conveyor_demo("A"), b=new_conveyor_demo("B");
    ASSERT_TRUE(a.conveyor_ids);
    EXPECT_NE(a.project_id,b.project_id);
    EXPECT_NE(a.conveyor_ids,b.conveyor_ids);
    EXPECT_EQ(sim::core::simulate(a.simulation),sim::core::simulate(b.simulation));
}
TEST(ConveyorProject, RejectsUnknownOrWrongRoleEntityReferences) {
    for(const auto path : {"/simulation/conveyor/axis_ref", "/simulation/conveyor/connections/0/source/entity",
                           "/simulation/conveyor/events/0/entity"}) {
        auto json=fixture();
        json[Json::json_pointer(path)]=json["project_id"];
        EXPECT_THROW(parse(json.dump()),Error);
    }
}
TEST(ConveyorProject, RejectsDuplicateAndMissingIdentities) {
    auto json=fixture();
    json["simulation"]["conveyor"]["motor_id"]=json["simulation"]["conveyor"]["piece_id"];
    EXPECT_THROW(parse(json.dump()),Error);
    auto doc=load(example);doc.conveyor_ids.reset();
    EXPECT_THROW(serialize(doc),Error);
    json=fixture();json["simulation"]["conveyor"].erase("plc_id");
    EXPECT_THROW(parse(json.dump()),Error);
}
TEST(ConveyorProject, RejectsAlgebraicFeedbackAndInvalidConnectionsFromJson) {
    auto json=fixture();
    json["simulation"]["conveyor"]["connections"][1]["timing"]="same_scan";
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["simulation"]["conveyor"]["connections"][1]["target"]["index"]=0;
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["simulation"]["conveyor"]["connections"][2]["target"]["port"]="plc_output";
    EXPECT_THROW(parse(json.dump()),Error);
}
TEST(ConveyorProject, RejectsInvalidTypedStateGeometryAndVersionMixes) {
    auto json=fixture();json["simulation"]["conveyor"]["motor_power"]="ON";
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["simulation"]["conveyor"]["piece_length_um"]=0;
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["simulation"]["conveyor"]["sensor_end_um"]=1000;
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["engine"]="discrete-v1";
    EXPECT_THROW(parse(json.dump()),Error);
    json=fixture();json["format_version"]["minor"]=2;
    EXPECT_THROW(parse(json.dump()),Error);
}
TEST(ConveyorProject, JsonFaultInjectionSurvivesSerializationAndDrivesPlant) {
    auto json=fixture();
    json["simulation"]["conveyor"]["events"].push_back({
        {"tick",2},{"sequence",1},{"entity",json["simulation"]["conveyor"]["motor_id"]},
        {"target","motor_power"},{"value","FAULT"}});
    const auto doc=parse(json.dump());
    const auto r=sim::core::simulate(parse(serialize(doc)).simulation);
    EXPECT_EQ(r.trace[2].conveyor->motor_state,sim::core::DigitalState::FAULT);
    EXPECT_EQ(r.trace.back().position_um,10);
}
}
