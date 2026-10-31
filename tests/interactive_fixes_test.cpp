#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/core/replay.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <barrier>
#include <chrono>
using namespace sim;
using namespace std::chrono_literals;
namespace {
using runtime::CommandCode;
using runtime::CommandKind;
using runtime::InteractiveSession;
using runtime::InteractiveState;
runtime::CommandReply finish(const runtime::CommandTicket& ticket) {
    if (ticket.completion.wait_for(10s) != std::future_status::ready) { throw std::runtime_error("Session request timeout"); }
    return ticket.completion.get();
}
runtime::SessionCommand advance(std::uint64_t session, std::uint64_t id, core::Tick ticks) {
    runtime::SessionCommand command{session, id, CommandKind::advance}; command.ticks = ticks; return command;
}
}
TEST(InteractiveFixes, StopBypassesExhaustedControlAndInputBudgets) {
    runtime::SessionLimits limits; limits.commands = 1; limits.functional_commands = 1;
    InteractiveSession session(1, core::conveyor_scenario(), limits);
    finish(session.submit({1, 1, CommandKind::start}));
    EXPECT_EQ(session.submit({1, 2, CommandKind::pause}).acceptance.code, CommandCode::history_limit);
    auto input = session.submit({1, 3, CommandKind::input, 0, core::ExternalTarget::command, core::DigitalState::HIGH});
    EXPECT_EQ(session.submit({1, 4, CommandKind::input, 0}).acceptance.code, CommandCode::history_limit);
    auto stop = session.submit({1, 5, CommandKind::stop});
    EXPECT_EQ(stop.acceptance.code, CommandCode::accepted);
    EXPECT_EQ(finish(stop).code, CommandCode::applied);
    EXPECT_EQ(finish(input).code, CommandCode::cancelled);
    EXPECT_EQ(session.publication()->state, InteractiveState::stopped);
}
TEST(InteractiveFixes, StopHasReservedSlotWhenOrdinaryQueueIsFull) {
    runtime::SessionLimits limits; limits.queue_capacity = 1;
    InteractiveSession session(1, core::conveyor_scenario(), limits);
    finish(session.submit({1, 1, CommandKind::start}));
    auto tickets = session.submit_batch({{1, 2, CommandKind::step}, {1, 3, CommandKind::step}, {1, 4, CommandKind::stop}});
    ASSERT_EQ(tickets[1].acceptance.code, CommandCode::queue_full);
    EXPECT_EQ(tickets[2].acceptance.code, CommandCode::accepted);
    EXPECT_EQ(finish(tickets[2]).code, CommandCode::applied);
    EXPECT_EQ(finish(tickets[0]).code, CommandCode::cancelled);
    EXPECT_EQ(session.publication()->next_tick, 0);
}
TEST(InteractiveFixes, StopRetainsResetEventsBeyondOrdinaryEventBudget) {
    auto spec = core::conveyor_scenario(); spec.conveyor->initial_command.state = core::DigitalState::HIGH;
    runtime::SessionLimits limits; limits.events = 9;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    for (std::uint64_t id = 3; id < 7; ++id) {
        ASSERT_EQ(finish(session.submit({1, id, id % 2 ? CommandKind::pause : CommandKind::resume})).code, CommandCode::applied);
    }
    EXPECT_EQ(finish(session.submit({1, 7, CommandKind::stop})).code, CommandCode::applied);
    auto history = session.history();
    ASSERT_TRUE(history->result.control);
    EXPECT_EQ(history->result.control->motor, core::DigitalState::LOW);
    EXPECT_GT(history->result.events.size(), limits.events);
    EXPECT_LE(history->result.events.size(), limits.events + 2 * spec.program.output_count + 2);
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveFixes, AdvanceReachesMaximumHorizonWithOneControlAdmission) {
    auto spec = core::conveyor_scenario(); spec.ticks = 100000;
    runtime::SessionLimits limits; limits.commands = 1; limits.advance_requests = 1; limits.snapshots = 1;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    const auto result = finish(session.submit(advance(1, 2, spec.ticks)));
    EXPECT_EQ(result.code, CommandCode::applied); EXPECT_EQ(result.advanced_ticks, spec.ticks);
    EXPECT_EQ(session.publication()->state, InteractiveState::completed);
    EXPECT_EQ(session.publication()->next_tick, spec.ticks);
    EXPECT_EQ(session.publication()->discarded_snapshots, spec.ticks - 1);
    const auto history = session.history();
    EXPECT_EQ(history->commands.size(), 2);
    EXPECT_EQ(history->replay->applied.size(), spec.ticks + 1);
}
TEST(InteractiveFixes, BatchAdvanceInterleavesFunctionalInputsAtExactBoundaries) {
    auto spec = core::conveyor_scenario(); spec.conveyor->events.clear();
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    auto tickets = session.submit_batch({advance(1, 2, 8),
        {1, 3, CommandKind::input, 2, core::ExternalTarget::command, core::DigitalState::HIGH},
        {1, 4, CommandKind::input, 5, core::ExternalTarget::command, core::DigitalState::LOW}});
    EXPECT_EQ(finish(tickets[0]).advanced_ticks, 8);
    EXPECT_EQ(finish(tickets[1]).tick, 2); EXPECT_EQ(finish(tickets[2]).tick, 5);
    const auto history = session.history();
    EXPECT_EQ(history->result.trace[1].conveyor->command.state, core::DigitalState::LOW);
    EXPECT_EQ(history->result.trace[2].conveyor->command.state, core::DigitalState::HIGH);
    EXPECT_EQ(history->result.trace[5].conveyor->command.state, core::DigitalState::LOW);
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveFixes, PauseSuspendsActiveAdvanceAndStopInterruptsIt) {
    auto spec = core::conveyor_scenario(); spec.ticks = 100000;
    InteractiveSession session(1, spec);
    finish(session.submit({1, 1, CommandKind::start}));
    auto tickets = session.submit_batch({advance(1, 2, spec.ticks), {1, 3, CommandKind::pause}});
    ASSERT_EQ(finish(tickets[1]).code, CommandCode::applied);
    EXPECT_EQ(tickets[0].completion.wait_for(0s), std::future_status::timeout);
    EXPECT_EQ(session.publication()->next_tick, 0);
    auto input = session.submit({1, 4, CommandKind::input, 0, core::ExternalTarget::command, core::DigitalState::HIGH});
    auto stopped = session.submit({1, 5, CommandKind::stop});
    EXPECT_EQ(finish(stopped).code, CommandCode::applied);
    EXPECT_EQ(finish(tickets[0]).code, CommandCode::cancelled);
    EXPECT_EQ(finish(input).code, CommandCode::cancelled);
}
TEST(InteractiveFixes, ReplayAfterCloseExcludesCancelledPendingInput) {
    InteractiveSession session(1, core::conveyor_scenario());
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    finish(session.submit({1, 3, CommandKind::pause}));
    auto cancelled = session.submit({1, 4, CommandKind::input, 1, core::ExternalTarget::command, core::DigitalState::HIGH});
    session.close();
    EXPECT_EQ(finish(cancelled).code, CommandCode::cancelled);
    const auto history = session.history();
    EXPECT_TRUE(std::any_of(history->commands.begin(), history->commands.end(), [](const auto& r) { return r.reply.code == CommandCode::cancelled; }));
    EXPECT_FALSE(std::any_of(history->replay->applied.begin(), history->replay->applied.end(), [](const auto& a) { return a.kind == CommandKind::input; }));
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveFixes, ReplayUsesNewIdentityAndOnlyAppliedOrder) {
    InteractiveSession original(1, core::conveyor_scenario());
    finish(original.submit({1, 1, CommandKind::start}));
    auto input = original.submit({1, 2, CommandKind::input, 0, core::ExternalTarget::command, core::DigitalState::HIGH});
    finish(original.submit({1, 3, CommandKind::pause})); finish(original.submit({1, 4, CommandKind::resume}));
    finish(original.submit({1, 5, CommandKind::step})); finish(input);
    auto cancelled = original.submit({1, 6, CommandKind::input, 1});
    original.close(); EXPECT_EQ(finish(cancelled).code, CommandCode::cancelled);
    const auto history = original.history();
    InteractiveSession fresh(999, history->replay->configuration, history->replay->limits);
    std::uint64_t id = 0;
    std::vector<runtime::CommandTicket> deferred;
    for (const auto& action : history->replay->applied) {
        auto ticket = fresh.submit({999, ++id, action.kind, action.tick, action.target, action.value});
        if (action.kind == CommandKind::input) { deferred.push_back(ticket); }
        else { EXPECT_EQ(finish(ticket).code, CommandCode::applied); }
    }
    for (const auto& ticket : deferred) { EXPECT_EQ(finish(ticket).code, CommandCode::applied); }
    EXPECT_EQ(fresh.publication()->session_id, 999);
    EXPECT_FALSE(core::compare_runs(history->result, fresh.history()->result));
}
TEST(InteractiveFixes, CloseDuringAdvanceReplaysOnlyCompletedTicks) {
    auto spec = core::conveyor_scenario(); spec.ticks = 100000;
    runtime::SessionLimits limits; limits.snapshots = 1;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    auto batch = session.submit(advance(1, 2, spec.ticks));
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (session.publication()->next_tick == 0 && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
    const auto close_done = session.request_close();
    ASSERT_EQ(close_done.wait_for(3s), std::future_status::ready);
    const auto result = finish(batch); const auto history = session.history();
    EXPECT_EQ(result.code, CommandCode::cancelled);
    EXPECT_GT(result.advanced_ticks, 0);
    EXPECT_LT(result.advanced_ticks, spec.ticks);
    EXPECT_EQ(result.advanced_ticks, history->publication.next_tick);
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveFixes, PausedSessionsDoNotOccupyRunnerAndCloseIndependently) {
    auto runner = std::make_shared<runtime::Runner>(8);
    InteractiveSession one(runner, 1, core::conveyor_scenario()), two(runner, 2, core::conveyor_scenario());
    finish(one.submit({1, 1, CommandKind::start})); finish(one.submit({1, 2, CommandKind::pause}));
    finish(two.submit({2, 1, CommandKind::start})); finish(two.submit({2, 2, CommandKind::pause}));
    std::vector<std::future<runtime::ResultPtr>> batches;
    for (int i = 0; i < 4; ++i) { batches.push_back(runner->submit(core::conveyor_scenario())); }
    for (auto& future : batches) {
        ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
        EXPECT_EQ(*future.get(), core::simulate(core::conveyor_scenario()));
    }
    std::jthread a([&] { one.close(); }); std::jthread b([&] { two.close(); }); a.join(); b.join();
    EXPECT_EQ(one.publication()->state, InteractiveState::closed);
    EXPECT_EQ(two.publication()->state, InteractiveState::closed);
}
TEST(InteractiveFixes, StopAcceptedBeforeCloseIsAppliedDespiteQueuedControls) {
    InteractiveSession session(1, core::conveyor_scenario()); finish(session.submit({1, 1, CommandKind::start}));
    auto tickets = session.submit_batch({{1, 2, CommandKind::step}, {1, 3, CommandKind::pause}, {1, 4, CommandKind::stop}});
    auto completion = session.request_close();
    ASSERT_EQ(completion.wait_for(3s), std::future_status::ready);
    EXPECT_EQ(finish(tickets[2]).code, CommandCode::applied);
    EXPECT_EQ(finish(tickets[0]).code, CommandCode::cancelled);
    EXPECT_EQ(finish(tickets[1]).code, CommandCode::cancelled);
    EXPECT_EQ(session.history()->result.control->state, core::SessionState::stopped);
}
TEST(InteractiveFixes, ConcurrentStepStopAndCloseAlwaysSettleReceipts) {
    for (int iteration = 0; iteration < 30; ++iteration) {
        InteractiveSession session(1, core::conveyor_scenario()); finish(session.submit({1, 1, CommandKind::start}));
        std::barrier gate(4);
        runtime::CommandTicket step, stop;
        std::jthread a([&] { gate.arrive_and_wait(); step = session.submit({1, 2, CommandKind::step}); });
        std::jthread b([&] { gate.arrive_and_wait(); stop = session.submit({1, 3, CommandKind::stop}); });
        std::jthread c([&] { gate.arrive_and_wait(); session.close(); });
        gate.arrive_and_wait(); a.join(); b.join(); c.join();
        const auto stop_result = finish(stop);
        if (stop.acceptance.code == CommandCode::accepted) { EXPECT_EQ(stop_result.code, CommandCode::applied); }
        else { EXPECT_EQ(stop_result.code, CommandCode::closed); }
        EXPECT_NE(finish(step).code, CommandCode::accepted);
        EXPECT_EQ(session.publication()->state, InteractiveState::closed);
        EXPECT_FALSE(core::compare_runs(session.history()->result, runtime::replay_session(*session.history()->replay)));
    }
}
TEST(InteractiveFixes, InvalidAdvanceAndMalformedReplayAreRejected) {
    InteractiveSession session(1, core::conveyor_scenario());
    EXPECT_EQ(session.submit(advance(1, 1, 0)).acceptance.code, CommandCode::invalid);
    EXPECT_EQ(session.submit(advance(1, 1, 9)).acceptance.code, CommandCode::invalid);
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    auto replay = *session.history()->replay;
    replay.applied.push_back({CommandKind::input, 1, core::ExternalTarget::command, core::DigitalState::HIGH});
    EXPECT_THROW(runtime::replay_session(replay), std::invalid_argument);
    replay.applied.back() = {CommandKind::step, 7};
    EXPECT_THROW(runtime::replay_session(replay), std::invalid_argument);
}

