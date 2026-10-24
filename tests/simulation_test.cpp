#include "sim/core/simulation.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <random>
using namespace sim::core;

TEST(Simulation, GoldenTraceConnectsElectricalLogicAndBoundedMotion) {
    const auto result = simulate(demo_scenario());
    const std::vector<std::int64_t> positions{0, 10, 20, 25, 25, 25, 25, 25};
    ASSERT_EQ(result.trace.size(), positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& s = result.trace[i];
        EXPECT_EQ(s.tick, i);
        EXPECT_EQ(s.time_us, (i + 1) * 1000);
        EXPECT_EQ(s.position_um, positions[i]);
        EXPECT_EQ(s.inputs[0], i >= 1 && i < 4);
        EXPECT_EQ(s.outputs[0], s.inputs[0]);
    }
}
TEST(Simulation, ReplayIsIdenticalAcross100ShuffledEventOrders) {
    auto spec = demo_scenario();
    spec.events = {{1, 20, 0, false}, {1, 30, 0, true}, {4, 40, 0, false}, {1, 10, 0, true}};
    const auto expected = simulate(spec);
    std::mt19937 random{42};
    for (int i = 0; i < 100; ++i) {
        std::shuffle(spec.events.begin(), spec.events.end(), random);
        EXPECT_EQ(simulate(spec), expected);
    }
    EXPECT_EQ(expected.trace[1].outputs[0], 1);
    EXPECT_EQ(expected.trace[4].outputs[0], 0);
}
TEST(Simulation, CoilsReadPreviousScanAndRungOrderDoesNotMatter) {
    auto spec = demo_scenario();
    spec.program.output_count = 2;
    spec.program.rungs.push_back(Rung{{Branch{Contact{Bank::output, 0, false}}}, 1});
    auto expected = simulate(spec);
    EXPECT_EQ(expected.trace[1].outputs[1], 0);
    EXPECT_EQ(expected.trace[2].outputs[1], 1);
    std::reverse(spec.program.rungs.begin(), spec.program.rungs.end());
    EXPECT_EQ(simulate(spec), expected);
}
TEST(Simulation, SeriesParallelAndNormallyClosedContacts) {
    auto spec = demo_scenario();
    spec.program.input_count = 3;
    spec.program.rungs = {Rung{{
        Branch{Contact{Bank::input, 0, false}, Contact{Bank::input, 1, true}},
        Branch{Contact{Bank::input, 2, false}}}, 0}};
    spec.ticks = 4;
    spec.events = {{0, 0, 0, true}, {1, 1, 1, true}, {2, 2, 2, true}, {3, 3, 2, false}};
    const auto r = simulate(spec);
    EXPECT_EQ(r.trace[0].outputs[0], 1);
    EXPECT_EQ(r.trace[1].outputs[0], 0);
    EXPECT_EQ(r.trace[2].outputs[0], 1);
    EXPECT_EQ(r.trace[3].outputs[0], 0);
}
TEST(Simulation, RejectsDuplicateSequenceAndOutOfRangeEvents) {
    auto spec = demo_scenario();
    spec.events[1].sequence = spec.events[0].sequence;
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec = demo_scenario();
    spec.events[1].tick = spec.ticks;
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec = demo_scenario();
    spec.events[0].signal = 1;
    EXPECT_THROW(simulate(spec), std::invalid_argument);
}
TEST(Simulation, RejectsInvalidProgramAndLimits) {
    auto spec = demo_scenario();
    spec.program.rungs.push_back(spec.program.rungs[0]);
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec = demo_scenario();
    spec.program.rungs[0].branches[0].clear();
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec = demo_scenario();
    spec.ticks = 0;
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec.ticks = 100001;
    EXPECT_THROW(simulate(spec), std::invalid_argument);
    spec = demo_scenario();
    spec.tick_period_us = std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(simulate(spec), std::invalid_argument);
}
TEST(Simulation, AxisSaturatesWithoutIntegerOverflow) {
    auto spec = demo_scenario();
    spec.axis.maximum_um = std::numeric_limits<std::int64_t>::max();
    spec.axis.initial_um = spec.axis.maximum_um - 1;
    spec.axis.travel_um_per_tick = spec.axis.maximum_um;
    EXPECT_EQ(simulate(spec).trace[1].position_um, spec.axis.maximum_um);
}
TEST(Simulation, CancellationDoesNotReturnPartialSuccess) {
    std::stop_source source;
    source.request_stop();
    EXPECT_THROW(simulate(demo_scenario(), source.get_token()), RunCancelled);
}
