#include "sim/project/document.hpp"
#include "sim/core/conveyor.hpp"
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
using namespace sim::project;
namespace {
TEST(TonProject, RoundTripPreservesIdentityPresetAndReplay) {
    auto doc=new_conveyor_demo("TON");
    doc.simulation.program.rungs[0].ton_us=2000;
    const auto text=serialize(doc);
    const auto restored=parse(text);
    EXPECT_EQ(restored.rung_ids,doc.rung_ids);
    EXPECT_EQ(serialize(restored),text);
    EXPECT_EQ(sim::core::simulate(restored.simulation),sim::core::simulate(doc.simulation));
    const auto json=nlohmann::json::parse(text);
    EXPECT_EQ(json["engine"],"conveyor-ton-v1");
    EXPECT_EQ(json["format_version"]["minor"],2);
}
TEST(TonProject, RejectsNegativeFractionalMissingAndOldProfileTimers) {
    auto doc=new_conveyor_demo("TON");doc.simulation.program.rungs[0].ton_us=2000;
    const auto original=nlohmann::json::parse(serialize(doc));
    for(const auto& value:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json("2000")}) {
        auto json=original;json["simulation"]["program"]["rungs"][0]["ton_us"]=value;
        EXPECT_THROW(parse(json.dump()),Error);
    }
    auto json=original;json["simulation"]["program"]["rungs"][0].erase("ton_us");
    EXPECT_THROW(parse(json.dump()),Error);
    json=original;json["format_version"]["minor"]=1;json["model_version"]=2;json["engine"]="conveyor-v1";
    EXPECT_THROW(parse(json.dump()),Error);
}
}
