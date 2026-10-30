#include "sim/core/replay.hpp"
#include "sim/core/conveyor.hpp"
#include <gtest/gtest.h>
#include <functional>
using namespace sim::core;
namespace {
RunResult sample() {
    auto spec=conveyor_scenario();
    spec.program.output_count=2;
    spec.program.rungs[0].ton_us=1000;
    spec.program.rungs.push_back({{Branch{{Bank::input,1,false}}},1,{},CounterConfig{1,{Bank::input,0,true}}});
    auto r=simulate(spec);
    r.control=SessionStatus{SessionState::completed,8,r.trace.back().position_um,r.trace.back().conveyor->motor_state};
    return r;
}
TEST(Replay, IdenticalCompleteAndEmptyResultsMatch) {
    const auto r=sample();
    EXPECT_FALSE(compare_runs(r,r));EXPECT_FALSE(compare_runs({},{}));
}
TEST(Replay, ReportsTickEntityVariableAndValuesForChangedPosition) {
    const auto expected=sample();auto actual=expected;actual.trace[3].position_um=123;
    const auto d=compare_runs(expected,actual);ASSERT_TRUE(d);
    EXPECT_EQ(d->tick,3);EXPECT_EQ(d->entity,"axis");
    EXPECT_EQ(d->variable,"trace[3].position_um");EXPECT_EQ(d->actual,"123");
    EXPECT_EQ(d->expected,std::to_string(expected.trace[3].position_um));
}
TEST(Replay, EverySnapshotStateFamilyIsCompared) {
    using Mutator=std::function<void(Snapshot&)>;
    const std::vector<Mutator> changes={
        [](auto& s){++s.tick;},[](auto& s){++s.time_us;},[](auto& s){++s.position_um;},
        [](auto& s){s.inputs.push_back(0);},[](auto& s){s.outputs[0]=3;},
        [](auto& s){++s.timers[0].elapsed_us;},[](auto& s){s.timers[0].active=!s.timers[0].active;},
        [](auto& s){s.timers[0].q=DigitalState::FAULT;},[](auto& s){++s.counters[1].cv;},
        [](auto& s){s.counters[1].armed=!s.counters[1].armed;},[](auto& s){s.counters[1].q=DigitalState::FAULT;},
        [](auto& s){s.conveyor.reset();},[](auto& s){s.conveyor->command.state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->motor_power.state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->sensor_power.state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->plc_inputs[0].state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->plc_outputs[0].state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->motor_command.state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->motor_state=DigitalState::FAULT;},
        [](auto& s){s.conveyor->sampled_sensor=DigitalState::FAULT;},
        [](auto& s){s.conveyor->sensor_state=DigitalState::FAULT;},
        [](auto& s){++s.conveyor->velocity_um_per_s;},
        [](auto& s){++s.conveyor->fractional_um_millionths;}
    };
    const auto r=sample();
    for(const auto& mutate:changes) {
        auto a=r;mutate(a.trace[3]);const auto d=compare_runs(r,a);
        ASSERT_TRUE(d);EXPECT_EQ(d->tick,3);EXPECT_NE(d->variable,"unmapped_state");
    }
}
TEST(Replay, MissingExtraAndReorderedEventsAreDetected) {
    const auto r=sample();ASSERT_GT(r.events.size(),2u);
    auto a=r;a.events.pop_back();EXPECT_TRUE(compare_runs(r,a));
    a=r;a.events.push_back(r.events.back());EXPECT_TRUE(compare_runs(r,a));
    a=r;std::swap(a.events[0],a.events[1]);EXPECT_TRUE(compare_runs(r,a));
    a=r;++a.events[0].time_us;EXPECT_TRUE(compare_runs(r,a));
    a=r;a.events[0].phase=Phase::control;EXPECT_TRUE(compare_runs(r,a));
    a=r;a.events[0].state=DigitalState::FAULT;EXPECT_TRUE(compare_runs(r,a));
}
TEST(Replay, CounterEventsAndControlAreComparedEvenWhenSnapshotsMatch) {
    const auto r=sample();ASSERT_FALSE(r.counter_events.empty());
    auto a=r;++a.counter_events[0].state.cv;
    auto d=compare_runs(r,a);ASSERT_TRUE(d);EXPECT_NE(d->variable.find("counter_event"),std::string::npos);
    a=r;a.counter_events[0].stopped=!a.counter_events[0].stopped;EXPECT_TRUE(compare_runs(r,a));
    a=r;a.control->state=SessionState::stopped;EXPECT_TRUE(compare_runs(r,a));
    a=r;a.control.reset();EXPECT_TRUE(compare_runs(r,a));
}
TEST(Replay, EarliestTickAcrossStoredStreamsWins) {
    const auto r=sample();auto a=r;
    ++a.trace[6].position_um;a.events[0].state=DigitalState::FAULT;
    const auto d=compare_runs(r,a);ASSERT_TRUE(d);EXPECT_EQ(d->tick,0);
}
TEST(Replay, TraceLengthMismatchUsesFirstMissingTick) {
    const auto r=sample();auto a=r;a.trace.pop_back();
    const auto d=compare_runs(r,a);ASSERT_TRUE(d);
    EXPECT_EQ(d->tick,7);EXPECT_EQ(d->variable,"length");EXPECT_EQ(d->entity,"trace");
}
TEST(Replay, ComparisonInspectionIsReadOnlyAndUsesProvidedPreviousImage) {
    auto spec=conveyor_scenario();spec.program.output_count=2;
    spec.program.rungs[0].branches={Branch{Contact::compare({{1},ComparisonOp::greater_equal,3})}};
    spec.program.rungs.push_back({{Branch{{Bank::input,1,false}}},1,{},CounterConfig{99,{Bank::input,0,true}}});
    std::vector<CounterState> previous(2);previous[1]={2,true,DigitalState::LOW};
    const auto before=previous;
    auto rows=comparison_values(spec.program,previous);
    ASSERT_EQ(rows.size(),4u);EXPECT_EQ(rows.back().value,"LOW");EXPECT_EQ(previous,before);
    previous[1].cv=3;rows=comparison_values(spec.program,previous);EXPECT_EQ(rows.back().value,"HIGH");
    previous[1].q=DigitalState::FAULT;rows=comparison_values(spec.program,previous);EXPECT_EQ(rows.back().value,"FAULT");
}
}
