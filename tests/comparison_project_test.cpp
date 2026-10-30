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
Document fixture() { return load(example("cv-compare-pulses.json")); }
TEST(ComparisonProject, JsonExampleExposesExactThirdCountBoundary) {
    const auto r=sim::core::simulate(fixture().simulation);
    EXPECT_EQ(r.trace[5].counters[1].cv,3);
    EXPECT_EQ(r.trace[5].outputs[0],0);
    EXPECT_EQ(r.trace[6].outputs[0],1);
    EXPECT_EQ(r.trace[6].outputs[1],0);
}
TEST(ComparisonProject, RoundTripPreservesIdentityReferenceAndFullReplay) {
    const auto doc=fixture();const auto encoded=serialize(doc);const auto restored=parse(encoded);
    EXPECT_EQ(doc.rung_ids,restored.rung_ids);EXPECT_EQ(doc.project_id,restored.project_id);
    EXPECT_EQ(serialize(restored),encoded);
    EXPECT_EQ(sim::core::simulate(doc.simulation),sim::core::simulate(restored.simulation));
    auto j=Json::parse(encoded);
    EXPECT_EQ(j["simulation"]["program"]["rungs"][0]["branches"][0][1]["compare"]["counter_ref"],doc.rung_ids[1]);
}
TEST(ComparisonProject, AllOperatorsAndSignedInt64ConstantsRoundTripExactly) {
    auto doc=fixture();auto& c=*doc.simulation.program.rungs[0].branches[0][1].comparison;
    using Op=sim::core::ComparisonOp;
    for(auto op:{Op::equal,Op::not_equal,Op::less,Op::less_equal,Op::greater,Op::greater_equal}) {
        for(auto value:{std::numeric_limits<std::int64_t>::min(),std::int64_t{-1},std::int64_t{0},std::numeric_limits<std::int64_t>::max()}) {
            c.op=op;c.constant=value;const auto restored=parse(serialize(doc));
            const auto& actual=*restored.simulation.program.rungs[0].branches[0][1].comparison;
            EXPECT_EQ(actual.op,op);EXPECT_EQ(actual.constant,value);
        }
    }
}
TEST(ComparisonProject, RejectsMalformedConstantsAndOutOfRangeLiterals) {
    const auto base=Json::parse(serialize(fixture()));
    for(const auto& v:{Json(1.5),Json("3"),Json(true),Json(nullptr),Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto j=base;j["simulation"]["program"]["rungs"][0]["branches"][0][1]["compare"]["constant"]=v;
        EXPECT_THROW(parse(j.dump()),Error);
    }
    auto text=base.dump();
    const auto pos=text.find("\"constant\":3");ASSERT_NE(pos,std::string::npos);
    text.replace(pos,std::string("\"constant\":3").size(),"\"constant\":-9223372036854775809");
    EXPECT_THROW(parse(text),Error);
}
TEST(ComparisonProject, RejectsUnknownOperatorMissingWrongOrAmbiguousCounter) {
    const auto base=Json::parse(serialize(fixture()));
    auto j=base;j["simulation"]["program"]["rungs"][0]["branches"][0][1]["compare"]["operator"]="=";
    EXPECT_THROW(parse(j.dump()),Error);
    j=base;j["simulation"]["program"]["rungs"][0]["branches"][0][1]["compare"]["counter_ref"]=j["project_id"];
    EXPECT_THROW(parse(j.dump()),Error);
    j=base;j["simulation"]["program"]["rungs"][0]["branches"][0][1]["compare"]["counter_ref"]=j["simulation"]["program"]["rungs"][0]["id"];
    EXPECT_THROW(parse(j.dump()),Error);
    j=base;j["simulation"]["program"]["rungs"].push_back(j["simulation"]["program"]["rungs"][1]);
    j["simulation"]["program"]["output_count"]=3;
    EXPECT_THROW(parse(j.dump()),Error);
}
TEST(ComparisonProject, UuidResolutionSurvivesRungReorderingAndNoCvIsPersisted) {
    auto j=Json::parse(serialize(fixture()));auto& r=j["simulation"]["program"]["rungs"];
    std::swap(r[0],r[1]);
    EXPECT_EQ(sim::core::simulate(parse(j.dump()).simulation),sim::core::simulate(fixture().simulation));
    r[1]["branches"][0][1]["compare"]["cv"]=3;
    EXPECT_THROW(parse(j.dump()),Error);
}
TEST(ComparisonProject, PreviousProfilesRemainCompatibleAndStrict) {
    for(const auto name:{"contactor-axis.json","conveyor.json","conveyor-ton.json","ctu-pulses.json","conveyor-ctu.json"}) {
        const auto doc=load(example(name));
        EXPECT_EQ(serialize(parse(serialize(doc))),serialize(doc));
    }
    auto j=Json::parse(serialize(fixture()));
    j["format_version"]["minor"]=3;j["model_version"]=4;j["engine"]="ladder-ctu-v1";
    EXPECT_THROW(parse(j.dump()),Error);
}
}
