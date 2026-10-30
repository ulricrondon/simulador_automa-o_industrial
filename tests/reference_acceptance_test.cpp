#include "sim/project/document.hpp"
#include "sim/core/replay.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
namespace {
using namespace sim::core;
using D=DigitalState;
sim::project::Document load(const char* name) {
    return sim::project::load(std::filesystem::path(SIM_FIXTURES_DIR).parent_path().parent_path()/"examples"/name);
}
TEST(ReferenceScenario, PhysicalGoldenSequenceHasOneRealSensorPassage) {
    const auto doc=load("reference-conveyor.json");const auto r=simulate(doc.simulation);
    const std::array<std::int64_t,12> positions{0,0,0,10,20,30,30,30,30,30,30,30};
    const std::array<std::uint64_t,12> elapsed{0,0,1000,2000,2000,2000,0,0,0,0,0,0};
    ASSERT_EQ(r.trace.size(),positions.size());
    for(std::size_t i=0;i<positions.size();++i) {
        SCOPED_TRACE(i);const auto& s=r.trace[i];
        EXPECT_EQ(s.position_um,positions[i]);EXPECT_EQ(s.time_us,(i+1)*1000);
        EXPECT_EQ(s.timers[0].elapsed_us,elapsed[i]);
        EXPECT_EQ(s.outputs[0],i>=3&&i<=5?1:0);
        EXPECT_EQ(s.counters[1].cv,i>=5?1:0);
        EXPECT_EQ(s.conveyor->sensor_state,i==4||i==5?D::HIGH:D::LOW);
        EXPECT_EQ(s.conveyor->sampled_sensor,i==5||i==6?D::HIGH:D::LOW);
    }
    EXPECT_EQ(std::count_if(r.events.begin(),r.events.end(),[](const auto& e){
        return e.kind==TransitionKind::sensor&&e.state==D::HIGH;
    }),1);
    EXPECT_EQ(r.trace[6].timers[0].q,D::LOW);EXPECT_FALSE(r.trace[6].timers[0].active);
}
TEST(ReferenceScenario, SensorCountAndComparisonRespectPhaseBoundary) {
    const auto doc=load("reference-conveyor.json");const auto r=simulate(doc.simulation);
    const auto high=std::find_if(r.events.begin(),r.events.end(),[](const auto& e){
        return e.kind==TransitionKind::sensor&&e.state==D::HIGH;
    });
    ASSERT_NE(high,r.events.end());EXPECT_EQ(high->tick,4);EXPECT_EQ(high->time_us,5000);
    const auto count=std::find_if(r.counter_events.begin(),r.counter_events.end(),[](const auto& e){return e.state.cv==1;});
    ASSERT_NE(count,r.counter_events.end());EXPECT_EQ(count->tick,5);EXPECT_EQ(count->time_us,5000);
    const auto prior=comparison_values(doc.simulation.program,r.trace[4].counters);
    const auto next=comparison_values(doc.simulation.program,r.trace[5].counters);
    EXPECT_EQ(prior[3].value,"HIGH");EXPECT_EQ(next[3].value,"LOW");
    EXPECT_EQ(r.trace[5].outputs[0],1);EXPECT_EQ(r.trace[6].outputs[0],0);
    for(std::size_t i=1;i<r.events.size();++i) {
        EXPECT_LE(r.events[i-1].time_us,r.events[i].time_us);
        if(r.events[i-1].tick==r.events[i].tick) {
            EXPECT_LE(static_cast<int>(r.events[i-1].phase),static_cast<int>(r.events[i].phase));
        }
    }
}
TEST(ReferenceScenario, PulseGoldenSequenceExercisesTonCtuComparisonsAndReset) {
    const auto doc=load("reference-pulses.json");const auto r=simulate(doc.simulation);
    const std::array<std::int64_t,15> cv{0,1,1,2,2,3,3,3,4,4,0,0,0,1,1};
    ASSERT_EQ(r.trace.size(),cv.size());
    for(std::size_t i=0;i<cv.size();++i) {
        EXPECT_EQ(r.trace[i].counters[1].cv,cv[i]);
        EXPECT_EQ(r.trace[i].outputs[0],i>=7&&i<=9?1:0);
        EXPECT_EQ(r.trace[i].timers[0].elapsed_us,i>=7&&i<=9?1000:0);
        EXPECT_EQ(r.trace[i].outputs[1],0); // PV=99, action depends on CV rather than Q.
    }
    EXPECT_EQ(r.trace.back().position_um,30);
}
TEST(ReferenceScenario, ConsecutiveRunnerReplaysReconstructTransientState) {
    sim::runtime::Runner runner;
    for(const auto file:{"reference-conveyor.json","reference-pulses.json"}) {
        const auto doc=load(file);const auto reference=runner.submit(doc.simulation).get();
        for(int i=0;i<4;++i) {
            const auto next=runner.submit(doc.simulation).get();
            EXPECT_FALSE(compare_runs(*reference,*next));
            EXPECT_EQ(next->trace.front().counters[1].cv,0);
            EXPECT_EQ(next->trace.front().timers[0].elapsed_us,0);
        }
    }
}
TEST(ReferenceScenario, ReloadReorderAndReplayKeepSameFunctionalSequence) {
    for(const auto file:{"reference-conveyor.json","reference-pulses.json"}) {
        auto doc=load(file);const auto reference=simulate(doc.simulation);
        std::reverse(doc.simulation.program.rungs.begin(),doc.simulation.program.rungs.end());
        std::reverse(doc.rung_ids.begin(),doc.rung_ids.end());
        std::reverse(doc.simulation.events.begin(),doc.simulation.events.end());
        const auto restored=sim::project::parse(sim::project::serialize(doc));
        EXPECT_FALSE(compare_runs(reference,simulate(restored.simulation)));
    }
}
TEST(ReferenceScenario, ControlledReplayPauseResumeStopIsIdenticalAndTerminal) {
    using A=ControlAction;
    const auto doc=load("reference-conveyor.json");
    const std::vector<A> actions{A::step,A::step,A::step,A::pause,A::step,A::resume,
                                 A::step,A::step,A::step,A::stop,A::step};
    sim::runtime::Runner runner;
    const auto a=runner.submit_controlled(doc.simulation,actions).get();
    const auto b=runner.submit_controlled(doc.simulation,actions).get();
    EXPECT_FALSE(compare_runs(*a,*b));ASSERT_TRUE(a->control);
    EXPECT_EQ(a->control->next_tick,6);EXPECT_EQ(a->control->state,SessionState::stopped);
    EXPECT_EQ(a->control->motor,D::LOW);EXPECT_TRUE(a->counter_events.back().stopped);
    EXPECT_EQ(a->counter_events.back().state.cv,0);
    const auto clean=runner.submit(doc.simulation).get();EXPECT_EQ(clean->trace.front().counters[1].cv,0);
}
TEST(ReferenceScenario, WallClockDelaysAndPauseDoNotChangeFunctionalSnapshots) {
    const auto doc=load("reference-conveyor.json");const auto reference=simulate(doc.simulation);
    ConveyorSession paced(doc.simulation);
    while(paced.state()==SessionState::running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));paced.step();
    }
    EXPECT_FALSE(compare_runs(reference,paced.result()));
    ConveyorSession paused(doc.simulation);paused.step();paused.pause();
    const auto before=paused.result().trace;EXPECT_FALSE(paused.step());
    EXPECT_EQ(paused.result().trace,before);paused.resume();
    while(paused.step()) {}
    EXPECT_EQ(paused.result().trace,reference.trace);
    EXPECT_EQ(paused.result().counter_events,reference.counter_events);
}
TEST(ReferenceScenario, InvalidSensorPowerPropagatesWithDocumentedDelay) {
    for(auto bad:{D::UNDEFINED,D::FAULT}) {
        auto doc=load("reference-conveyor.json");
        doc.simulation.conveyor->events.push_back({2,1,ExternalTarget::sensor_power,bad});
        const auto r=simulate(doc.simulation);
        EXPECT_EQ(r.trace[2].conveyor->sensor_state,D::FAULT==bad?D::FAULT:D::UNDEFINED);
        EXPECT_EQ(r.trace[3].conveyor->sampled_sensor,bad);
        EXPECT_EQ(r.trace[3].counters[1].q,bad);
        const auto comparisons=comparison_values(doc.simulation.program,r.trace[3].counters);
        EXPECT_EQ(comparisons[3].value,std::string(name(bad)));
        EXPECT_EQ(r.trace.back().position_um,0);
        EXPECT_FALSE(compare_runs(r,simulate(doc.simulation)));
    }
}
TEST(ReferenceScenario, MotorFaultStopsMotionAndMechanicalLimitRemainsBounded) {
    auto doc=load("reference-conveyor.json");
    doc.simulation.conveyor->events.push_back({4,1,ExternalTarget::motor_power,D::FAULT});
    const auto fault=simulate(doc.simulation);
    EXPECT_EQ(fault.trace[4].conveyor->motor_state,D::FAULT);
    EXPECT_EQ(fault.trace.back().position_um,10);
    auto spec=load("reference-conveyor.json").simulation;
    spec.axis.maximum_um=30;
    const auto limited=simulate(spec);
    EXPECT_EQ(limited.trace.back().position_um,20);
    for(const auto& s:limited.trace) EXPECT_LE(s.position_um+spec.conveyor->piece_length_um,spec.axis.maximum_um);
}
TEST(ReferenceScenario, InspectionDoesNotMutateResultsOrExecution) {
    const auto doc=load("reference-conveyor.json");auto result=simulate(doc.simulation);const auto before=result;
    std::vector<CounterState> previous(doc.simulation.program.output_count);
    for(const auto& s:result.trace) {
        EXPECT_FALSE(snapshot_values(s).empty());EXPECT_FALSE(comparison_values(doc.simulation.program,previous).empty());
        previous=s.counters;
    }
    EXPECT_EQ(result,before);EXPECT_FALSE(compare_runs(before,simulate(doc.simulation)));
}
TEST(ReferenceScenario, EveryEarlierExampleStillLoadsAndReplays) {
    for(const auto file:{"contactor-axis.json","conveyor.json","conveyor-ton.json","ctu-pulses.json",
                        "conveyor-ctu.json","cv-compare-pulses.json"}) {
        const auto doc=load(file);const auto restored=sim::project::parse(sim::project::serialize(doc));
        EXPECT_FALSE(compare_runs(simulate(doc.simulation),simulate(restored.simulation)));
    }
}
}
