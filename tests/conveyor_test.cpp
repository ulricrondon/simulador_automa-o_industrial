#include "sim/core/conveyor.hpp"
#include "sim/runtime/runner.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>

namespace {
using namespace sim::core;
using D = DigitalState;
RunSpec moving() {
    auto spec = conveyor_scenario();
    spec.conveyor->initial_command.state = D::HIGH;
    spec.conveyor->events.clear();
    spec.conveyor->sensor_begin_um = 90;
    spec.conveyor->sensor_end_um = 100;
    return spec;
}
TEST(Electrical, FourStateTruthTablesAndTypedRoles) {
    static_assert(!std::is_convertible_v<PowerSupply, DriveCommand>);
    static_assert(!std::is_same_v<PlcInput, PlcOutput>);
    constexpr std::array states{D::LOW, D::HIGH, D::UNDEFINED, D::FAULT};
    constexpr D and_table[4][4] = {
        {D::LOW,D::LOW,D::LOW,D::FAULT}, {D::LOW,D::HIGH,D::UNDEFINED,D::FAULT},
        {D::LOW,D::UNDEFINED,D::UNDEFINED,D::FAULT}, {D::FAULT,D::FAULT,D::FAULT,D::FAULT}};
    constexpr D or_table[4][4] = {
        {D::LOW,D::HIGH,D::UNDEFINED,D::FAULT}, {D::HIGH,D::HIGH,D::HIGH,D::FAULT},
        {D::UNDEFINED,D::HIGH,D::UNDEFINED,D::FAULT}, {D::FAULT,D::FAULT,D::FAULT,D::FAULT}};
    for (std::size_t i = 0; i < states.size(); ++i) {
        for (std::size_t j = 0; j < states.size(); ++j) {
            EXPECT_EQ(series(states[i],states[j]), and_table[i][j]);
            EXPECT_EQ(parallel(states[i],states[j]), or_table[i][j]);
        }
    }
    EXPECT_EQ(invert(D::LOW), D::HIGH); EXPECT_EQ(invert(D::HIGH), D::LOW);
    EXPECT_EQ(invert(D::UNDEFINED), D::UNDEFINED); EXPECT_EQ(invert(D::FAULT), D::FAULT);
    EXPECT_THROW(invert(static_cast<D>(9)), std::invalid_argument);
}
TEST(Conveyor, ClosedLoopGoldenTraceStopsOnPhotoelectricFeedback) {
    const auto result = simulate(conveyor_scenario());
    const std::array<std::int64_t, 8> expected{0,10,20,20,20,20,20,20};
    ASSERT_EQ(result.trace.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(result.trace[i].position_um, expected[i]);
        EXPECT_EQ(result.trace[i].time_us, (i + 1) * 1000);
    }
    const auto& detected = *result.trace[2].conveyor;
    EXPECT_EQ(detected.sampled_sensor, D::LOW);
    EXPECT_EQ(detected.sensor_state, D::HIGH);
    EXPECT_EQ(detected.motor_state, D::HIGH);
    const auto& stopped = *result.trace[3].conveyor;
    EXPECT_EQ(stopped.plc_inputs[1].state, D::HIGH);
    EXPECT_EQ(stopped.plc_outputs[0].state, D::LOW);
    EXPECT_EQ(stopped.motor_command.state, D::LOW);
    EXPECT_EQ(stopped.motor_state, D::LOW);
}
TEST(Conveyor, OperatorCommandTurnsMotorOffAndOn) {
    auto spec = moving();
    spec.conveyor->events = {{1,0,ExternalTarget::command,D::LOW}, {2,1,ExternalTarget::command,D::HIGH}};
    const auto result = simulate(spec);
    EXPECT_EQ(result.trace[0].position_um,10);
    EXPECT_EQ(result.trace[1].position_um,10);
    EXPECT_EQ(result.trace[1].conveyor->motor_state,D::LOW);
    EXPECT_EQ(result.trace[2].position_um,20);
    EXPECT_EQ(result.trace[2].conveyor->motor_state,D::HIGH);
}
TEST(Conveyor, MotorPowerIsSeparateFromLadderCommand) {
    auto spec = moving();
    spec.conveyor->events = {{1,0,ExternalTarget::motor_power,D::LOW},
                             {2,1,ExternalTarget::motor_power,D::HIGH},
                             {3,2,ExternalTarget::motor_power,D::FAULT}};
    const auto r = simulate(spec);
    EXPECT_EQ(r.trace[1].conveyor->motor_command.state,D::HIGH);
    EXPECT_EQ(r.trace[1].conveyor->motor_state,D::LOW);
    EXPECT_EQ(r.trace[1].position_um,10);
    EXPECT_EQ(r.trace[2].position_um,20);
    EXPECT_EQ(r.trace[3].conveyor->motor_state,D::FAULT);
    EXPECT_EQ(r.trace[7].position_um,20);
}
TEST(Conveyor, UndefinedAndFaultSensorPropagateOnNextScan) {
    for (const auto power : {D::LOW, D::UNDEFINED, D::FAULT}) {
        auto spec = moving();
        spec.conveyor->events = {{0,0,ExternalTarget::sensor_power,power}};
        const auto r = simulate(spec);
        const auto expected = power == D::FAULT ? D::FAULT : D::UNDEFINED;
        EXPECT_EQ(r.trace[0].conveyor->sampled_sensor,D::LOW);
        EXPECT_EQ(r.trace[0].conveyor->sensor_state,expected);
        EXPECT_EQ(r.trace[1].conveyor->plc_inputs[1].state,expected);
        EXPECT_EQ(r.trace[1].conveyor->motor_state,expected);
        EXPECT_EQ(r.trace[7].position_um,10);
    }
}
TEST(Conveyor, UndefinedOperatorCommandNeverBecomesAnActiveCommand) {
    auto spec = moving();
    spec.conveyor->initial_command.state = D::UNDEFINED;
    const auto r = simulate(spec);
    EXPECT_EQ(r.trace[0].conveyor->motor_state,D::UNDEFINED);
    EXPECT_EQ(r.trace.back().position_um,0);
}
TEST(Conveyor, SensorGeometryUsesHalfOpenIntervals) {
    for (const auto& [position, expected] : std::array<std::pair<std::int64_t,D>,3>{
             std::pair{15,D::LOW}, std::pair{20,D::HIGH}, std::pair{30,D::LOW}}) {
        auto spec = conveyor_scenario();
        spec.axis.initial_um = position;
        spec.conveyor->events.clear();
        const auto r = simulate(spec);
        EXPECT_EQ(r.trace[0].conveyor->sampled_sensor,expected);
        EXPECT_EQ(r.trace[0].conveyor->sensor_state,expected);
    }
}
TEST(Conveyor, SweptDetectionDoesNotLoseAPieceCrossingInOneStep) {
    auto spec = conveyor_scenario();
    spec.conveyor->events.clear();
    spec.conveyor->initial_command.state = D::HIGH;
    spec.conveyor->speed_um_per_s = 40000;
    const auto r = simulate(spec);
    EXPECT_EQ(r.trace[0].position_um,40);
    EXPECT_EQ(r.trace[0].conveyor->sensor_state,D::HIGH);
    EXPECT_EQ(r.trace[1].conveyor->sampled_sensor,D::HIGH);
    EXPECT_EQ(r.trace[1].conveyor->motor_state,D::LOW);
    EXPECT_EQ(r.trace[1].position_um,40);
    EXPECT_EQ(r.trace[1].conveyor->sensor_state,D::LOW);
}
TEST(Conveyor, FixedPointRemainderPreservesVelocityAcrossTimesteps) {
    auto a = moving(); a.ticks=4; a.conveyor->speed_um_per_s=333;
    auto b = a; b.ticks=2; b.tick_period_us=2000;
    const auto ra=simulate(a), rb=simulate(b);
    EXPECT_EQ(ra.trace.back().time_us,rb.trace.back().time_us);
    EXPECT_EQ(ra.trace.back().position_um,1);
    EXPECT_EQ(ra.trace.back().position_um,rb.trace.back().position_um);
    EXPECT_EQ(ra.trace.back().conveyor->fractional_um_millionths,332000);
    EXPECT_EQ(ra.trace.back().conveyor->fractional_um_millionths,
              rb.trace.back().conveyor->fractional_um_millionths);
}
TEST(Conveyor, AxisLimitRespectsPieceLengthWithoutOverflow) {
    auto spec=moving();
    spec.axis.maximum_um=std::numeric_limits<std::int64_t>::max();
    spec.axis.initial_um=spec.axis.maximum_um-spec.conveyor->piece_length_um-1;
    const auto r=simulate(spec);
    EXPECT_EQ(r.trace[0].position_um,spec.axis.maximum_um-spec.conveyor->piece_length_um);
    EXPECT_EQ(r.trace[0].conveyor->velocity_um_per_s,0);
    EXPECT_EQ(r.trace.back().position_um,r.trace.front().position_um);
}
TEST(Conveyor, TypedEventOrderAndTransitionsRepeatAcrossPermutations) {
    auto spec=moving();
    spec.conveyor->events={{0,1,ExternalTarget::command,D::LOW},{0,2,ExternalTarget::command,D::HIGH},
                           {2,3,ExternalTarget::motor_power,D::LOW},{3,4,ExternalTarget::motor_power,D::HIGH}};
    const auto expected=simulate(spec);
    std::mt19937 random{73};
    for(int i=0;i<100;++i) {
        std::shuffle(spec.conveyor->events.begin(),spec.conveyor->events.end(),random);
        EXPECT_EQ(simulate(spec),expected);
    }
}
TEST(Conveyor, TransitionPhasesAndSensorFeedbackHaveExplicitTiming) {
    const auto r=simulate(conveyor_scenario());
    ASSERT_FALSE(r.events.empty());
    for(std::size_t i=1;i<r.events.size();++i) {
        EXPECT_LE(r.events[i-1].time_us,r.events[i].time_us);
        if(r.events[i-1].tick==r.events[i].tick) { EXPECT_LE(r.events[i-1].phase,r.events[i].phase); }
    }
    const auto sensor=std::find_if(r.events.begin(),r.events.end(),[](const auto& e){
        return e.kind==TransitionKind::sensor && e.state==D::HIGH;
    });
    ASSERT_NE(sensor,r.events.end());
    EXPECT_EQ(sensor->tick,2); EXPECT_EQ(sensor->time_us,3000);
    const auto feedback=std::find_if(r.events.begin(),r.events.end(),[](const auto& e){
        return e.kind==TransitionKind::plc_input && e.signal==1 && e.state==D::HIGH;
    });
    ASSERT_NE(feedback,r.events.end());
    EXPECT_EQ(feedback->tick,3); EXPECT_EQ(feedback->time_us,3000);
    EXPECT_LT(sensor,feedback);
}
TEST(Conveyor, OutputFeedbackReadsPreviousScanRegardlessOfRungOrder) {
    auto spec=moving();
    spec.program.output_count=2;
    spec.program.rungs.push_back(Rung{{Branch{{Bank::output,0,false}}},1});
    spec.axis.drive=1; spec.conveyor->connections[2].source.index=1;
    const auto expected=simulate(spec);
    EXPECT_EQ(expected.trace[0].position_um,0);
    EXPECT_EQ(expected.trace[1].position_um,10);
    std::reverse(spec.program.rungs.begin(),spec.program.rungs.end());
    EXPECT_EQ(simulate(spec),expected);
}
TEST(Conveyor, UnwiredPlcInputsRemainUndefined) {
    auto spec=moving();spec.program.input_count=3;
    spec.program.rungs[0].branches[0].push_back({Bank::input,2,false});
    const auto r=simulate(spec);
    EXPECT_EQ(r.trace[0].conveyor->plc_inputs[2].state,D::UNDEFINED);
    EXPECT_EQ(r.trace.back().position_um,0);
}
TEST(Conveyor, RejectsInvalidDirectionsDuplicateDriversAndInstantFeedback) {
    auto spec=conveyor_scenario();
    spec.conveyor->connections[1].timing=Timing::same_scan;
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=conveyor_scenario();spec.conveyor->connections[1].target.index=0;
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=conveyor_scenario();std::swap(spec.conveyor->connections[2].source,spec.conveyor->connections[2].target);
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=conveyor_scenario();spec.conveyor->connections.pop_back();
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=conveyor_scenario();spec.conveyor->connections[2].source.index=1;
    EXPECT_THROW(validate(spec),std::invalid_argument);
    spec=conveyor_scenario();spec.conveyor->connections[0].source.index=1;
    EXPECT_THROW(validate(spec),std::invalid_argument);
}
TEST(Conveyor, RejectsInvalidGeometrySpeedAndCompetingInputEvents) {
    auto s=conveyor_scenario();s.conveyor->speed_um_per_s=0;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->speed_um_per_s=-1;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->piece_length_um=101;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.axis.initial_um=95;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->sensor_end_um=25;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->speed_um_per_s=std::numeric_limits<std::int64_t>::max();
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.events={{0,99,0,true}};
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.axis.travel_um_per_tick=1;
    EXPECT_THROW(validate(s),std::invalid_argument);
}
TEST(Conveyor, RejectsInvalidFunctionalEvents) {
    auto s=conveyor_scenario();s.conveyor->events.push_back(s.conveyor->events[0]);
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->events[0].tick=s.ticks;
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->events[0].value=static_cast<D>(5);
    EXPECT_THROW(validate(s),std::invalid_argument);
    s=conveyor_scenario();s.conveyor->events[0].target=static_cast<ExternalTarget>(8);
    EXPECT_THROW(validate(s),std::invalid_argument);
}
TEST(ConveyorControl, PauseFreezesTimeAndResumePreservesPhysicalReplay) {
    const auto spec=conveyor_scenario();
    ConveyorSession session(spec);
    ASSERT_TRUE(session.step()); ASSERT_TRUE(session.step());
    const auto prefix=session.result();
    session.pause(); session.pause();
    for(int i=0;i<10;++i) { EXPECT_FALSE(session.step()); }
    EXPECT_EQ(session.next_tick(),2);
    EXPECT_EQ(session.position_um(),10);
    EXPECT_EQ(session.result().trace,prefix.trace);
    session.resume();session.resume();
    while(session.step()) {}
    auto controlled=session.result();
    std::erase_if(controlled.events,[](const auto& e){
        return e.kind==TransitionKind::paused || e.kind==TransitionKind::resumed;
    });
    EXPECT_EQ(controlled,simulate(spec));
}
TEST(ConveyorControl, StopDeenergizesAndIsTerminalWithoutAddingSimulatedTime) {
    ConveyorSession session(moving());
    ASSERT_TRUE(session.step());
    EXPECT_EQ(session.motor_state(),D::HIGH);
    session.stop();session.stop();
    EXPECT_EQ(session.state(),SessionState::stopped);
    EXPECT_EQ(session.motor_state(),D::LOW);
    EXPECT_EQ(session.next_tick(),1);
    EXPECT_EQ(session.position_um(),10);
    EXPECT_FALSE(session.step());
    EXPECT_THROW(session.resume(),std::logic_error);
    EXPECT_THROW(session.pause(),std::logic_error);
    ASSERT_FALSE(session.result().events.empty());
    EXPECT_EQ(session.result().events.back().kind,TransitionKind::stopped);
    EXPECT_EQ(session.result().events.back().time_us,1000);
}
TEST(ConveyorControl, StopWhilePausedCancelsAllFutureFunctionalEvents) {
    ConveyorSession session(conveyor_scenario());
    session.pause();session.stop();
    EXPECT_FALSE(session.step());
    EXPECT_EQ(session.next_tick(),0);
    EXPECT_EQ(session.motor_state(),D::LOW);
    EXPECT_TRUE(session.result().trace.empty());
}
TEST(ConveyorControl, TerminalAndInvalidSessionRulesAreExplicit) {
    EXPECT_THROW(ConveyorSession{demo_scenario()},std::invalid_argument);
    auto spec=conveyor_scenario();spec.ticks=2;
    ConveyorSession session(spec);
    while(session.step()) {}
    EXPECT_EQ(session.state(),SessionState::completed);
    EXPECT_THROW(session.pause(),std::logic_error);
    EXPECT_THROW(session.resume(),std::logic_error);
    const auto result=session.result();session.stop();
    EXPECT_EQ(session.result(),result);
}
TEST(Conveyor, ExistingDedicatedRunnerExecutesTheSameScenario) {
    const auto spec=conveyor_scenario();
    sim::runtime::Runner runner;
    EXPECT_EQ(*runner.submit(spec).get(),simulate(spec));
}
TEST(Conveyor, CancellationDoesNotPublishPartialSuccess) {
    std::stop_source source;source.request_stop();
    EXPECT_THROW(simulate(conveyor_scenario(),source.get_token()),RunCancelled);
}
TEST(ConveyorControl, DedicatedRunnerPreservesControlBoundariesAndRecoversFromInvalidSequence) {
    using A = ControlAction;
    sim::runtime::Runner runner;
    const auto spec = conveyor_scenario();
    const auto result = runner.submit_controlled(spec, {A::step,A::step,A::pause,A::step,A::resume,A::step,A::stop,A::step}).get();
    ASSERT_TRUE(result->control);
    EXPECT_EQ(result->control->state,SessionState::stopped);
    EXPECT_EQ(result->control->next_tick,3);
    EXPECT_EQ(result->control->position_um,20);
    EXPECT_EQ(result->control->motor,D::LOW);
    ConveyorSession direct(spec);
    direct.step();direct.step();direct.pause();direct.step();direct.resume();direct.step();direct.stop();direct.step();
    EXPECT_EQ(result->trace,direct.result().trace);
    EXPECT_EQ(result->events,direct.result().events);
    EXPECT_THROW(runner.submit_controlled(spec,{A::stop,A::resume}).get(),std::logic_error);
    EXPECT_EQ(*runner.submit(spec).get(),simulate(spec));
    EXPECT_THROW(runner.submit_controlled(demo_scenario(),{}),std::invalid_argument);
    EXPECT_THROW(runner.submit_controlled(spec,{static_cast<A>(99)}),std::invalid_argument);
}
TEST(ConveyorControl, ConsumingResultTransfersCompleteHistoryWithoutCopying) {
    static_assert(std::is_same_v<decltype(std::declval<ConveyorSession&>().result()),const RunResult&>);
    static_assert(std::is_same_v<decltype(std::declval<const ConveyorSession&>().result()),const RunResult&>);
    static_assert(std::is_same_v<decltype(std::declval<ConveyorSession>().result()),RunResult>);
    const auto spec=conveyor_scenario();
    ConveyorSession session(spec);
    while(session.step()) {}
    const auto expected=session.result();
    const auto* storage=session.result().trace.data();
    const auto moved=std::move(session).result();
    EXPECT_EQ(moved.trace.data(),storage);
    EXPECT_EQ(moved,expected);
    EXPECT_EQ(moved,simulate(spec));
}

}
