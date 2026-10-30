#include "sim/core/conveyor.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
using namespace sim::core;
namespace {
using D=DigitalState;using Op=ComparisonOp;
Contact cv(Op op,std::int64_t value,Signal coil=1) { return Contact::compare({{coil},op,value}); }
RunSpec pulses() {
    auto s=demo_scenario();s.ticks=9;s.program.input_count=3;s.program.output_count=2;
    s.program.rungs={
        {{Branch{cv(Op::greater_equal,3)}},0},
        {{Branch{{Bank::input,1,false}}},1,{},CounterConfig{99,{Bank::input,2,false}}}
    };
    s.events={{1,0,1,true},{2,1,1,false},{3,2,1,true},{4,3,1,false},{5,4,1,true}};
    return s;
}
RunSpec conveyor() {
    auto s=conveyor_scenario();s.program.output_count=2;
    s.program.rungs[0].branches={Branch{{Bank::input,0,false},cv(Op::less,1)}};
    s.program.rungs.push_back({{Branch{{Bank::input,1,false}}},1,{},CounterConfig{99,{Bank::input,0,true}}});
    return s;
}
TEST(Comparison, SixOperatorsBelowEqualAndAboveThreshold) {
    for(std::int64_t value:{2,3,4}) {
        CounterState state{value,false,D::LOW};
        const bool expected[]={value==3,value!=3,value<3,value<=3,value>3,value>=3};
        const Op ops[]={Op::equal,Op::not_equal,Op::less,Op::less_equal,Op::greater,Op::greater_equal};
        for(std::size_t i=0;i<6;++i)
            EXPECT_EQ(evaluate_comparison(state,{{1},ops[i],3}),expected[i]?D::HIGH:D::LOW);
    }
}
TEST(Comparison, ZeroNegativeAndInt64ExtremesRemainExact) {
    const auto max=std::numeric_limits<std::int64_t>::max();
    const auto min=std::numeric_limits<std::int64_t>::min();
    EXPECT_EQ(evaluate_comparison({},{{1},Op::equal,0}),D::HIGH);
    EXPECT_EQ(evaluate_comparison({},{{1},Op::greater,min}),D::HIGH);
    EXPECT_EQ(evaluate_comparison({},{{1},Op::less,-1}),D::LOW);
    EXPECT_EQ(evaluate_comparison({max,false,D::LOW},{{1},Op::equal,max}),D::HIGH);
    EXPECT_EQ(evaluate_comparison({max-1,false,D::LOW},{{1},Op::less,max}),D::HIGH);
    EXPECT_EQ(evaluate_comparison({max,false,D::LOW},{{1},Op::greater,min}),D::HIGH);
}
TEST(Comparison, DiagnosticsPropagateRegardlessOfNumericRelation) {
    for(auto bad:{D::UNDEFINED,D::FAULT}) {
        for(auto op:{Op::equal,Op::not_equal,Op::less,Op::less_equal,Op::greater,Op::greater_equal}) {
            EXPECT_EQ(evaluate_comparison({3,true,bad},{{1},op,3}),bad);
        }
    }
}
TEST(Comparison, InvalidOperatorAndCounterStateRejected) {
    EXPECT_THROW(evaluate_comparison({},{{1},static_cast<Op>(99),0}),std::invalid_argument);
    EXPECT_THROW(evaluate_comparison({-1,false,D::LOW},{{1},Op::equal,0}),std::invalid_argument);
    EXPECT_THROW(evaluate_comparison({0,false,static_cast<D>(99)},{{1},Op::equal,0}),std::invalid_argument);
}
TEST(Comparison, EvaluatorNeverMutatesCounter) {
    const CounterState original{3,true,D::HIGH};auto state=original;
    for(int i=0;i<50;++i) EXPECT_EQ(evaluate_comparison(state,{{1},Op::greater_equal,3}),D::HIGH);
    EXPECT_EQ(state,original);
}
TEST(ComparisonLadder, ThirdPulseIsVisibleOnlyOnFollowingScanAndIndependentOfQ) {
    const auto r=simulate(pulses());
    EXPECT_EQ(r.trace[5].counters[1].cv,3);
    EXPECT_EQ(r.trace[5].outputs[0],0);
    EXPECT_EQ(r.trace[6].outputs[0],1);
    EXPECT_EQ(r.trace[6].outputs[1],0); // PV=99, Q is still LOW.
}
TEST(ComparisonLadder, RungOrderCannotChangeSnapshotsOrEvents) {
    auto s=pulses();const auto baseline=simulate(s);
    std::reverse(s.program.rungs.begin(),s.program.rungs.end());
    EXPECT_EQ(simulate(s),baseline);
    sim::runtime::Runner runner;
    for(int i=0;i<5;++i) EXPECT_EQ(*runner.submit(s).get(),baseline);
}
TEST(ComparisonLadder, AndOrWithContactsAndMultipleReadsDoNotCountTwice) {
    auto s=pulses();
    s.program.rungs[0].branches={Branch{{Bank::input,0,false},cv(Op::greater_equal,3)},
                                 Branch{cv(Op::equal,2),cv(Op::less_equal,2)}};
    const auto r=simulate(s);
    EXPECT_EQ(r.trace[4].outputs[0],1); // second OR branch at CV=2
    EXPECT_EQ(r.trace[6].outputs[0],0); // first branch inhibited by input 0
    const auto baseline=simulate(pulses());
    EXPECT_EQ(r.counter_events,baseline.counter_events);
    for(std::size_t i=0;i<r.trace.size();++i) EXPECT_EQ(r.trace[i].counters,baseline.trace[i].counters);
}
TEST(ComparisonLadder, ResetAndDiagnosticUseCoherentPreviousCounterImage) {
    auto p=pulses().program;
    std::vector<TimerState> t(2);std::vector<CounterState> c(2);std::vector<PlcOutput> o(2);
    c[1]={3,false,D::FAULT};
    o=scan_ladder(p,{{D::LOW},{D::LOW},{D::HIGH}},o,t,c,1000);
    EXPECT_EQ(o[0].state,D::FAULT);EXPECT_EQ(c[1].cv,0);
    o=scan_ladder(p,{{D::LOW},{D::LOW},{D::LOW}},o,t,c,1000);
    EXPECT_EQ(o[0].state,D::LOW);
}
TEST(ComparisonLadder, SelfReferenceIsDelayedAndHasNoInstantaneousLoop) {
    auto s=pulses();s.program.rungs[1].branches={Branch{cv(Op::less,3)}};
    validate(s);
    std::vector<TimerState> t(2);std::vector<CounterState> c(2);std::vector<PlcOutput> o(2);
    c[1]={2,true,D::LOW};
    o=scan_ladder(s.program,{{D::LOW},{D::LOW},{D::LOW}},o,t,c,1000);
    EXPECT_EQ(c[1].cv,3);EXPECT_EQ(o[0].state,D::LOW);
    o=scan_ladder(s.program,{{D::LOW},{D::LOW},{D::LOW}},o,t,c,1000);
    EXPECT_EQ(c[1].cv,3);EXPECT_EQ(o[0].state,D::HIGH);
}
TEST(ComparisonLadder, TonConsumesComparisonWithoutChangingItsTimeSemantics) {
    auto s=pulses();s.program.rungs[0].ton_us=1000;
    const auto r=simulate(s);
    EXPECT_EQ(r.trace[6].outputs[0],0);EXPECT_EQ(r.trace[7].outputs[0],1);
    EXPECT_EQ(r.trace[7].timers[0].elapsed_us,1000);
}
TEST(ComparisonLadder, MissingWrongKindAndAmbiguousReferenceRejectedBeforeScan) {
    auto s=pulses();s.program.rungs[0].branches={Branch{cv(Op::equal,0,99)}};
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=pulses();s.program.rungs[1].ctu.reset();EXPECT_THROW(validate(s),std::invalid_argument);
    s=pulses();s.program.rungs.push_back(s.program.rungs[1]);
    EXPECT_THROW(validate(s),std::invalid_argument);
    std::vector<TimerState> t(2);std::vector<CounterState> c(2);const auto before=c;
    EXPECT_THROW(scan_ladder(s.program,{{D::LOW},{D::LOW},{D::LOW}},std::vector<PlcOutput>(2),t,c,1000),std::invalid_argument);
    EXPECT_EQ(c,before);
}
TEST(ComparisonLadder, MixedContactAndComparisonOrResetComparisonRejected) {
    auto s=pulses();s.program.rungs[0].branches[0][0].normally_closed=true;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=pulses();s.program.rungs[1].ctu->reset=cv(Op::equal,0);
    EXPECT_THROW(validate(s),std::invalid_argument);
}
TEST(ComparisonLadder, ConveyorPauseResumeAndStopPreserveBoundaryRules) {
    auto s=conveyor();const auto full=simulate(s);
    ConveyorSession session(s);
    for(int i=0;i<4;++i) session.step();
    const auto state=session.counters();const auto trace=session.result().trace;
    session.pause();for(int i=0;i<4;++i) EXPECT_FALSE(session.step());
    EXPECT_EQ(session.counters(),state);EXPECT_EQ(session.result().trace,trace);
    session.resume();session.step();
    EXPECT_EQ(session.result().trace.back(),full.trace[4]);
    EXPECT_EQ(session.motor_state(),D::LOW);
    session.stop();EXPECT_EQ(session.counters()[1],CounterState{});
    EXPECT_FALSE(session.step());EXPECT_THROW(session.resume(),std::logic_error);
}
}
