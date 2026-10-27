#include "sim/core/conveyor.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
using namespace sim::core;
namespace {
using D = DigitalState;
CounterState sample(CounterState s, D cu, D reset=D::LOW, std::int64_t pv=3) {
    return update_ctu(s,cu,reset,pv);
}
RunSpec sensor_spec() {
    auto s=conveyor_scenario();
    s.program.output_count=2;
    s.program.rungs[0].branches={Branch{{Bank::input,0,false},{Bank::output,1,true}}};
    s.program.rungs.push_back({{Branch{{Bank::input,1,false}}},1,{},CounterConfig{1,{Bank::input,0,true}}});
    return s;
}
TEST(Ctu, InitialHighDoesNotInventEdge) {
    auto s=sample({},D::HIGH);
    EXPECT_EQ(s.cv,0);EXPECT_FALSE(s.armed);EXPECT_EQ(s.q,D::LOW);
    for(int i=0;i<5;++i) s=sample(s,D::HIGH);
    EXPECT_EQ(s.cv,0);
}
TEST(Ctu, CountsOnlyRepeatedLowHighEdgesAndHoldsOnLow) {
    auto s=sample({},D::LOW);
    s=sample(s,D::HIGH);EXPECT_EQ(s.cv,1);
    for(int i=0;i<5;++i) s=sample(s,D::HIGH);
    EXPECT_EQ(s.cv,1);
    s=sample(s,D::LOW);EXPECT_EQ(s.cv,1);
    s=sample(s,D::HIGH);EXPECT_EQ(s.cv,2);
}
TEST(Ctu, ReachesPresetAndContinuesCountingBeyondIt) {
    CounterState s;
    for(int i=1;i<=5;++i) {
        s=sample(sample(s,D::LOW),D::HIGH);
        EXPECT_EQ(s.cv,i);
        EXPECT_EQ(s.q,i>=3?D::HIGH:D::LOW);
    }
}
TEST(Ctu, ResetWinsOverEdgeAndDisarmsUntilNewLow) {
    auto s=sample(sample({},D::LOW),D::HIGH);
    s=sample(s,D::LOW);
    s=sample(s,D::HIGH,D::HIGH);
    EXPECT_EQ(s.cv,0);EXPECT_EQ(s.q,D::LOW);EXPECT_FALSE(s.armed);
    s=sample(s,D::HIGH);EXPECT_EQ(s.cv,0);
    s=sample(sample(s,D::LOW),D::HIGH);EXPECT_EQ(s.cv,1);
    s=sample(s,D::FAULT,D::HIGH);EXPECT_EQ(s.cv,0);EXPECT_EQ(s.q,D::LOW);
}
TEST(Ctu, ZeroPresetInitialResetAndRecoveryAreExplicit) {
    EXPECT_EQ(CounterState{}.q,D::LOW);
    auto s=sample({},D::HIGH,D::LOW,0);
    EXPECT_EQ(s.cv,0);EXPECT_EQ(s.q,D::HIGH);
    s=sample(s,D::HIGH,D::HIGH,0);EXPECT_EQ(s.q,D::LOW);
    s=sample(s,D::HIGH,D::LOW,0);EXPECT_EQ(s.cv,0);EXPECT_EQ(s.q,D::HIGH);
}
TEST(Ctu, SaturatesAtInt64MaxWithoutOverflow) {
    const auto limit=std::numeric_limits<std::int64_t>::max();
    CounterState s{limit-1,true,D::LOW};
    s=sample(s,D::HIGH,D::LOW,limit);
    EXPECT_EQ(s.cv,limit);EXPECT_EQ(s.q,D::HIGH);
    for(int i=0;i<3;++i) s=sample(sample(s,D::LOW,D::LOW,limit),D::HIGH,D::LOW,limit);
    EXPECT_EQ(s.cv,limit);
}
TEST(Ctu, UnknownOrFaultCuHoldsCountAndRequiresRearming) {
    for(auto bad:{D::UNDEFINED,D::FAULT}) {
        CounterState s{2,true,D::LOW};
        s=sample(s,bad);EXPECT_EQ(s.cv,2);EXPECT_EQ(s.q,bad);EXPECT_FALSE(s.armed);
        s=sample(s,D::HIGH);EXPECT_EQ(s.cv,2);
        s=sample(sample(s,D::LOW),D::HIGH);EXPECT_EQ(s.cv,3);EXPECT_EQ(s.q,D::HIGH);
    }
}
TEST(Ctu, UnknownOrFaultResetInhibitsCountAndPreservesCv) {
    for(auto bad:{D::UNDEFINED,D::FAULT}) {
        CounterState s{2,true,D::LOW};
        s=sample(s,D::HIGH,bad);EXPECT_EQ(s.cv,2);EXPECT_EQ(s.q,bad);EXPECT_FALSE(s.armed);
        s=sample(s,D::HIGH);EXPECT_EQ(s.cv,2);
        s=sample(sample(s,D::LOW),D::HIGH);EXPECT_EQ(s.cv,3);
    }
    EXPECT_EQ(sample({},D::FAULT,D::UNDEFINED).q,D::FAULT);
    EXPECT_EQ(sample({},D::UNDEFINED,D::FAULT).q,D::FAULT);
}
TEST(Ctu, AllFourStateTransitionPairsCountOnlyKnownLowHigh) {
    for(auto a:{D::LOW,D::HIGH,D::UNDEFINED,D::FAULT})
        for(auto b:{D::LOW,D::HIGH,D::UNDEFINED,D::FAULT}) {
            const auto s=sample(sample({},a),b);
            EXPECT_EQ(s.cv,(a==D::LOW&&b==D::HIGH)?1:0);
        }
}
TEST(Ctu, RejectsInvalidStateAndPreset) {
    EXPECT_THROW(sample({},D::LOW,D::LOW,-1),std::invalid_argument);
    EXPECT_THROW(sample(CounterState{-1,false,D::LOW},D::LOW),std::invalid_argument);
    EXPECT_THROW(sample({},static_cast<D>(9)),std::invalid_argument);
    EXPECT_THROW(sample({},D::LOW,static_cast<D>(9)),std::invalid_argument);
}
TEST(CtuLadder, GeometricSensorCountsAndQStopsMotorOnNextScan) {
    const auto r=simulate(sensor_spec());
    EXPECT_EQ(r.trace[2].counters[1].cv,0);
    EXPECT_EQ(r.trace[3].counters[1].cv,1);
    EXPECT_EQ(r.trace[3].outputs[1],1);
    EXPECT_EQ(r.trace[3].outputs[0],1);
    EXPECT_EQ(r.trace[4].outputs[0],0);
    EXPECT_EQ(r.trace.back().position_um,30);
    EXPECT_EQ(r.trace.back().counters[1].cv,1);
}
TEST(CtuLadder, RungOrderAndReplayProduceIdenticalEvents) {
    auto spec=sensor_spec();const auto expected=simulate(spec);
    std::reverse(spec.program.rungs.begin(),spec.program.rungs.end());
    EXPECT_EQ(simulate(spec),expected);
    sim::runtime::Runner runner;
    for(int i=0;i<10;++i) EXPECT_EQ(*runner.submit(spec).get(),expected);
    ASSERT_FALSE(expected.counter_events.empty());
    EXPECT_EQ(expected.counter_events.back().time_us,5000); // Sensor LOW rearms at tick 5.
}
TEST(CtuLadder, PausePreservesEdgeMemoryAndQWithoutDuplicateCounts) {
    ConveyorSession s(sensor_spec());
    s.step();s.step();s.step();
    const auto before=s.counters();s.pause();
    for(int i=0;i<5;++i) EXPECT_FALSE(s.step());
    EXPECT_EQ(s.counters(),before);
    s.resume();s.step();
    EXPECT_EQ(s.counters()[1].cv,1);EXPECT_EQ(s.counters()[1].q,D::HIGH);
    const auto high=s.counters();s.pause();EXPECT_FALSE(s.step());s.resume();s.step();
    EXPECT_EQ(s.counters(),high);
}
TEST(CtuLadder, StopClearsTransientCountAndEmitsTerminalReset) {
    ConveyorSession s(sensor_spec());for(int i=0;i<4;++i)s.step();
    ASSERT_EQ(s.counters()[1].cv,1);
    s.stop();EXPECT_EQ(s.counters()[1],CounterState{});
    EXPECT_EQ(s.motor_state(),D::LOW);EXPECT_FALSE(s.step());
    ASSERT_TRUE(s.result().counter_events.back().stopped);
    EXPECT_EQ(s.result().counter_events.back().state.cv,0);
    EXPECT_EQ(s.result().trace.back().counters[1].cv,1);
    EXPECT_THROW(s.resume(),std::logic_error);
}
TEST(CtuLadder, DuplicateInstanceIsRejectedBeforeMutation) {
    auto spec=sensor_spec();spec.program.rungs.push_back(spec.program.rungs[1]);
    EXPECT_THROW(validate(spec),std::invalid_argument);
    std::vector<TimerState> timers(2);std::vector<CounterState> counters(2);
    counters[1]={3,true,D::HIGH};const auto before=counters;
    EXPECT_THROW(scan_ladder(spec.program,std::vector<PlcInput>(2),std::vector<PlcOutput>(2),timers,counters,1000),std::invalid_argument);
    EXPECT_EQ(counters,before);
}
TEST(CtuLadder, InvalidResetMixedBlockAndMissingStateRejected) {
    auto spec=sensor_spec();spec.program.rungs[1].ctu->reset.signal=99;
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=sensor_spec();spec.program.rungs[1].ctu->reset.bank=static_cast<Bank>(99);
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=sensor_spec();spec.program.rungs[1].ton_us=1000;
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=sensor_spec();std::vector<TimerState> timers(2);
    EXPECT_THROW(scan_ladder(spec.program,std::vector<PlcInput>(2),std::vector<PlcOutput>(2),timers,1000),std::invalid_argument);
}
TEST(CtuLadder, TonCanFollowCounterQUsingPreviousImage) {
    auto spec=sensor_spec();spec.program.output_count=3;
    spec.program.rungs.push_back({{Branch{{Bank::output,1,false}}},2,1000});
    const auto r=simulate(spec);
    EXPECT_EQ(r.trace[3].outputs[1],1);
    EXPECT_EQ(r.trace[4].outputs[2],0);
    EXPECT_EQ(r.trace[5].outputs[2],1);
    EXPECT_EQ(r.trace[5].timers[2].elapsed_us,1000);
}
TEST(CtuLadder, NoNcAndOrContactsStillFeedCuAndReset) {
    Program p{3,1,{{{Branch{{Bank::input,0,false},{Bank::input,1,true}},Branch{{Bank::input,2,false}}},0,{},CounterConfig{1,{Bank::output,0,false}}}}};
    std::vector<TimerState> t(1);std::vector<CounterState> c(1);std::vector<PlcOutput> o(1);
    o=scan_ladder(p,{{D::LOW},{D::LOW},{D::LOW}},o,t,c,1);
    o=scan_ladder(p,{{D::HIGH},{D::LOW},{D::LOW}},o,t,c,1);
    EXPECT_EQ(c[0].cv,1);EXPECT_EQ(o[0].state,D::HIGH);
    o=scan_ladder(p,{{D::HIGH},{D::LOW},{D::LOW}},o,t,c,1);
    EXPECT_EQ(c[0].cv,0);EXPECT_EQ(o[0].state,D::LOW);
    o=scan_ladder(p,{{D::LOW},{D::HIGH},{D::LOW}},o,t,c,1);
    o=scan_ladder(p,{{D::LOW},{D::HIGH},{D::HIGH}},o,t,c,1);
    EXPECT_EQ(c[0].cv,1);
}
}
