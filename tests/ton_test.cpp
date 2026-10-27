#include "sim/core/conveyor.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
using namespace sim::core;
namespace {
RunSpec timed(std::uint64_t preset=2000) {
    auto s=conveyor_scenario();s.program.rungs[0].ton_us=preset;return s;
}
TEST(Ton, ClosedLoopStartsAtDeadlineAndStopsAtSensor) {
    const auto r=simulate(timed());
    const std::vector<std::int64_t> expected{0,0,0,10,20,20,20,20};
    for(std::size_t i=0;i<expected.size();++i) EXPECT_EQ(r.trace[i].position_um,expected[i]);
    EXPECT_EQ(r.trace[1].timers[0].elapsed_us,0);
    EXPECT_EQ(r.trace[2].timers[0].elapsed_us,1000);
    EXPECT_EQ(r.trace[3].timers[0].q,DigitalState::HIGH);
    EXPECT_EQ(r.trace[5].timers[0].elapsed_us,0);
}
TEST(Ton, ZeroDelayAndNonMultiplePreset) {
    auto zero=simulate(timed(0));
    auto plain=simulate(conveyor_scenario());
    for(std::size_t i=0;i<zero.trace.size();++i) {
        EXPECT_EQ(zero.trace[i].position_um,plain.trace[i].position_um);
        EXPECT_EQ(zero.trace[i].outputs,plain.trace[i].outputs);
    }
    const auto r=simulate(timed(1500));
    EXPECT_EQ(r.trace[2].outputs[0],0);
    EXPECT_EQ(r.trace[3].outputs[0],1);
    EXPECT_EQ(r.trace[3].timers[0].elapsed_us,1500);
}
TEST(Ton, LowUnknownAndFaultResetWithoutRetainingElapsedTime) {
    for(auto state:{DigitalState::LOW,DigitalState::UNDEFINED,DigitalState::FAULT}) {
        auto s=timed();
        s.conveyor->events.push_back({3,1,ExternalTarget::command,state});
        s.conveyor->events.push_back({4,2,ExternalTarget::command,DigitalState::HIGH});
        const auto r=simulate(s);
        EXPECT_EQ(r.trace[3].timers[0].elapsed_us,0);
        EXPECT_EQ(r.trace[3].timers[0].q,state);
        EXPECT_EQ(r.trace[4].timers[0].elapsed_us,0);
        EXPECT_EQ(r.trace[5].outputs[0],0);
        EXPECT_EQ(r.trace[6].outputs[0],1);
    }
}
TEST(Ton, PauseFreezesTimerAndStopIsTerminal) {
    ConveyorSession session(timed());
    session.step();session.step();session.step();
    const auto before=session.result().trace;
    session.pause();
    for(int i=0;i<10;++i) EXPECT_FALSE(session.step());
    EXPECT_EQ(session.result().trace,before);
    session.resume();session.step();
    EXPECT_EQ(session.result().trace.back().timers[0].q,DigitalState::HIGH);
    session.stop();
    EXPECT_EQ(session.motor_state(),DigitalState::LOW);
    EXPECT_FALSE(session.step());
    EXPECT_THROW(session.resume(),std::logic_error);
}
TEST(Ton, SaturationAvoidsOverflow) {
    auto s=demo_scenario(); s.ticks=3;
    s.tick_period_us=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())/2;
    s.program.rungs[0].ton_us=s.tick_period_us+1;
    s.events={{0,0,0,true}};
    const auto r=simulate(s);
    EXPECT_EQ(r.trace[1].outputs[0],0);
    EXPECT_EQ(r.trace[2].outputs[0],1);
    EXPECT_EQ(r.trace[2].timers[0].elapsed_us,*s.program.rungs[0].ton_us);
}
TEST(Ton, IndependentTimersAndPreviousOutputContactsIgnoreRungOrder) {
    auto s=demo_scenario();s.program.output_count=2;s.events={{0,0,0,true}};
    s.program.rungs[0].ton_us=1000;
    s.program.rungs.push_back({{Branch{{Bank::output,0,false}}},1,2000});
    const auto a=simulate(s);
    std::reverse(s.program.rungs.begin(),s.program.rungs.end());
    EXPECT_EQ(a,simulate(s));
    EXPECT_EQ(a.trace[1].outputs[0],1);
    EXPECT_EQ(a.trace[3].outputs[1],0);
    EXPECT_EQ(a.trace[4].outputs[1],1);
}
TEST(Ton, ReplayAcrossRunnerJobsResetsRuntimeState) {
    auto s=timed();s.conveyor->events.push_back({6,1,ExternalTarget::command,DigitalState::LOW});
    const auto expected=simulate(s);
    std::reverse(s.conveyor->events.begin(),s.conveyor->events.end());
    sim::runtime::Runner runner;
    for(int i=0;i<10;++i) EXPECT_EQ(*runner.submit(s).get(),expected);
}
TEST(Ton, InvalidPresetAndStatelessInvocationAreRejected) {
    auto s=timed();s.program.rungs[0].ton_us=std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=timed();
    EXPECT_THROW(scan_ladder(s.program,std::vector<PlcInput>(2),std::vector<PlcOutput>(1)),std::invalid_argument);
}
}
