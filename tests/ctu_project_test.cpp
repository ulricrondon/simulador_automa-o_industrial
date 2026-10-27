#include "sim/project/document.hpp"
#include "sim/core/conveyor.hpp"
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <algorithm>
using namespace sim::project;
namespace {
using Json=nlohmann::json;
std::filesystem::path example(const char* name) {
    return std::filesystem::path(SIM_FIXTURES_DIR).parent_path().parent_path()/"examples"/name;
}
TEST(CtuProject, PulseFixtureCountsBeyondPresetResetsAndControlsAction) {
    const auto doc=load(example("ctu-pulses.json"));
    const auto r=sim::core::simulate(doc.simulation);
    EXPECT_EQ(r.trace[1].counters[1].cv,1);
    EXPECT_EQ(r.trace[5].counters[1].cv,3);
    EXPECT_EQ(r.trace[6].outputs[0],0);
    EXPECT_EQ(r.trace[8].counters[1].cv,4);
    EXPECT_EQ(r.trace[10].counters[1].cv,0);
    EXPECT_EQ(r.trace[11].counters[1].cv,0);
    EXPECT_EQ(r.trace[13].counters[1].cv,1);
}
TEST(CtuProject, RoundTripPreservesUuidsReferencesAndNoRuntimeState) {
    const auto doc=load(example("ctu-pulses.json"));const auto encoded=serialize(doc);
    const auto restored=parse(encoded);
    EXPECT_EQ(restored.rung_ids,doc.rung_ids);EXPECT_EQ(restored.axis_id,doc.axis_id);
    EXPECT_EQ(serialize(restored),encoded);
    EXPECT_EQ(sim::core::simulate(restored.simulation),sim::core::simulate(doc.simulation));
    auto json=Json::parse(encoded);
    EXPECT_FALSE(json["simulation"]["program"]["rungs"][1]["ctu"].contains("cv"));
    json["simulation"]["program"]["rungs"][1]["ctu"]["cv"]=3;
    EXPECT_THROW(parse(json.dump()),Error);
}
TEST(CtuProject, SensorFixturePreservesConveyorIdentityAndReplay) {
    const auto doc=load(example("conveyor-ctu.json"));
    const auto restored=parse(serialize(doc));
    EXPECT_EQ(restored.conveyor_ids,doc.conveyor_ids);
    EXPECT_EQ(sim::core::simulate(restored.simulation).trace.back().counters[1].cv,1);
}
TEST(CtuProject, InvalidPresetReferencesIdentitiesAndDualInstructionRejected) {
    const auto original=Json::parse(serialize(load(example("ctu-pulses.json"))));
    for(const auto& v:{Json(-1),Json(1.5),Json("3"),Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto j=original;j["simulation"]["program"]["rungs"][1]["ctu"]["pv"]=v;
        EXPECT_THROW(parse(j.dump()),Error);
    }
    auto j=original;j["simulation"]["program"]["rungs"][1]["ctu"]["reset"]["signal"]=99;
    EXPECT_THROW(parse(j.dump()),Error);
    j=original;j["simulation"]["program"]["rungs"][1]["id"]=j["simulation"]["program"]["rungs"][0]["id"];
    EXPECT_THROW(parse(j.dump()),Error);
    j=original;j["simulation"]["program"]["rungs"][1]["ton_us"]=1;
    EXPECT_THROW(parse(j.dump()),Error);
    j=original;j["simulation"]["program"]["rungs"][1].erase("ctu");
    EXPECT_THROW(parse(j.dump()),Error);
}
TEST(CtuProject, Int64MaximumAndZeroPresetAreExact) {
    auto doc=load(example("ctu-pulses.json"));
    for(const auto pv:{std::int64_t{0},std::numeric_limits<std::int64_t>::max()}) {
        doc.simulation.program.rungs[1].ctu->pv=pv;
        EXPECT_EQ(parse(serialize(doc)).simulation.program.rungs[1].ctu->pv,pv);
    }
}
TEST(CtuProject, OldProfilesRemainAndCannotSilentlyAcceptCounter) {
    for(const auto file:{"contactor-axis.json","conveyor.json","conveyor-ton.json"}) {
        const auto doc=load(example(file));
        const auto encoded=serialize(doc);
        EXPECT_FALSE(Json::parse(encoded)["simulation"]["program"]["rungs"][0].contains("ctu"));
        EXPECT_EQ(serialize(parse(encoded)),encoded);
    }
    auto j=Json::parse(serialize(load(example("conveyor-ctu.json"))));
    j["format_version"]["minor"]=1;j["model_version"]=2;j["engine"]="conveyor-v1";
    EXPECT_THROW(parse(j.dump()),Error);
}
TEST(CtuProject, UnorderedInputEventsHaveIdenticalFullReplay) {
    auto doc=load(example("ctu-pulses.json"));const auto result=sim::core::simulate(doc.simulation);
    std::reverse(doc.simulation.events.begin(),doc.simulation.events.end());
    EXPECT_EQ(sim::core::simulate(parse(serialize(doc)).simulation),result);
}
}
