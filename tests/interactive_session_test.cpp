#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/core/replay.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <array>
#include <chrono>
#include <type_traits>
using namespace sim;
using namespace std::chrono_literals;
namespace {
using runtime::CommandKind;
using runtime::CommandCode;
using runtime::InteractiveState;
struct Harness {
    std::shared_ptr<runtime::Runner> runner = std::make_shared<runtime::Runner>(8);
    runtime::InteractiveSession session;
    std::uint64_t id{};
    explicit Harness(runtime::SessionLimits limits = {}, core::RunSpec spec = core::conveyor_scenario())
        : session(runner, 1, std::move(spec), limits) {}
    runtime::CommandReply send(CommandKind kind) {
        auto ticket = session.submit({1, ++id, kind});
        if (ticket.completion.wait_for(3s) != std::future_status::ready) { throw std::runtime_error("Command timeout"); }
        return ticket.completion.get();
    }
    runtime::CommandTicket input(core::Tick tick, core::DigitalState value,
                                 core::ExternalTarget target = core::ExternalTarget::command) {
        return session.submit({1, ++id, CommandKind::input, tick, target, value});
    }
};
}
TEST(Interactive, TransitionsAndImmutablePublication) {
    Harness h;
    EXPECT_EQ(h.session.publication()->state, InteractiveState::loaded);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::invalid);
    EXPECT_EQ(h.send(CommandKind::start).code, CommandCode::applied);
    EXPECT_EQ(h.send(CommandKind::start).code, CommandCode::invalid);
    h.send(CommandKind::step);
    auto first = h.session.publication();
    static_assert(std::is_const_v<decltype(first)::element_type>);
    static_assert(std::is_const_v<decltype(first->latest)::element_type>);
    ASSERT_TRUE(first->latest);
    EXPECT_EQ(first->latest->tick, 0);
    EXPECT_EQ(first->latest->time_us, 1000);
    h.send(CommandKind::pause);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::invalid);
    EXPECT_EQ(h.session.publication()->next_tick, 1);
    h.send(CommandKind::resume); h.send(CommandKind::step);
    EXPECT_EQ(first->latest->tick, 0);
    EXPECT_LT(first->sequence, h.session.publication()->sequence);
    h.send(CommandKind::stop);
    EXPECT_EQ(h.session.publication()->state, InteractiveState::stopped);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::invalid);
    auto history = h.session.history();
    ASSERT_TRUE(history->result.control);
    EXPECT_EQ(history->result.control->state, core::SessionState::stopped);
    h.session.close(); h.session.close();
    EXPECT_EQ(h.session.publication()->state, InteractiveState::closed);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::closed);
    EXPECT_EQ(history->publication.state, InteractiveState::stopped);
}
TEST(Interactive, BoundaryInputIsDeferredAndAcknowledgedInOrder) {
    Harness h;
    h.send(CommandKind::start);
    auto input = h.input(0, core::DigitalState::HIGH);
    EXPECT_EQ(input.acceptance.code, CommandCode::accepted);
    EXPECT_EQ(input.completion.wait_for(0s), std::future_status::timeout);
    const auto step = h.send(CommandKind::step);
    const auto applied = input.completion.get();
    EXPECT_EQ(applied.code, CommandCode::applied);
    EXPECT_EQ(applied.tick, 0);
    EXPECT_LT(applied.application_sequence, step.application_sequence);
    EXPECT_EQ(h.session.publication()->latest->conveyor->command.state, core::DigitalState::HIGH);
    auto stale = h.input(0, core::DigitalState::LOW);
    EXPECT_EQ(stale.completion.get().code, CommandCode::stale);
}
TEST(Interactive, PausedCloseCancelsPendingInputsAndPreservesPublishedObjects) {
    Harness h;
    h.send(CommandKind::start); h.send(CommandKind::step); h.send(CommandKind::pause);
    auto view = h.session.publication();
    auto input = h.input(1, core::DigitalState::HIGH);
    h.session.close();
    EXPECT_EQ(input.completion.get().code, CommandCode::cancelled);
    EXPECT_EQ(view->latest->tick, 0);
    EXPECT_EQ(h.session.publication()->state, InteractiveState::closed);
}
TEST(Interactive, StopCancelsPendingInputWithoutApplyingIt) {
    Harness h; h.send(CommandKind::start);
    auto input = h.input(0, core::DigitalState::HIGH);
    h.send(CommandKind::stop);
    EXPECT_EQ(input.completion.get().code, CommandCode::cancelled);
    EXPECT_TRUE(h.session.history()->result.trace.empty());
}
TEST(Interactive, RejectsWrongSessionDuplicateIdsAndInvalidInputs) {
    Harness h; h.send(CommandKind::start);
    EXPECT_EQ(h.session.submit({99, 2, CommandKind::step}).acceptance.code, CommandCode::stale);
    EXPECT_EQ(h.session.submit({1, 1, CommandKind::step}).acceptance.code, CommandCode::stale);
    auto invalid = h.input(0, static_cast<core::DigitalState>(99));
    EXPECT_EQ(invalid.acceptance.code, CommandCode::invalid);
    EXPECT_EQ(h.input(0, core::DigitalState::LOW, static_cast<core::ExternalTarget>(99)).acceptance.code, CommandCode::invalid);
    EXPECT_EQ(h.session.submit({1, 100, static_cast<CommandKind>(99)}).acceptance.code, CommandCode::invalid);
}
TEST(Interactive, QueueFullIsDeterministicAndRejectedIdCanBeRetried) {
    runtime::SessionLimits limits; limits.queue_capacity = 1;
    runtime::InteractiveSession session(2, core::conveyor_scenario(), limits);
    auto tickets = session.submit_batch({{2, 1, CommandKind::start}, {2, 2, CommandKind::step}});
    EXPECT_EQ(tickets[1].acceptance.code, CommandCode::queue_full);
    EXPECT_EQ(tickets[1].completion.get().code, CommandCode::queue_full);
    EXPECT_EQ(tickets[0].completion.get().code, CommandCode::applied);
    EXPECT_EQ(session.submit({2, 2, CommandKind::step}).completion.get().code, CommandCode::applied);
}
TEST(Interactive, BoundedHistoryEvictsOnlySnapshotsAndReportsRange) {
    runtime::SessionLimits limits; limits.snapshots = 2;
    Harness h(limits); h.send(CommandKind::start);
    for (int i = 0; i < 8; ++i) { EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::applied); }
    auto history = h.session.history();
    EXPECT_EQ(history->publication.state, InteractiveState::completed);
    EXPECT_EQ(history->result.trace.size(), 2);
    EXPECT_EQ(history->publication.first_tick, 6);
    EXPECT_EQ(history->publication.last_tick, 7);
    EXPECT_EQ(history->publication.discarded_snapshots, 6);
    const auto expected = core::simulate(core::conveyor_scenario());
    EXPECT_EQ(history->result.events, expected.events);
    EXPECT_EQ(history->result.counter_events, expected.counter_events);
    EXPECT_EQ(history->commands.size(), 9);
}
TEST(Interactive, FullHistoryBudgetFailsBeforeStepAndKeepsEvidence) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 2;
    Harness h(limits); h.send(CommandKind::start); h.send(CommandKind::step); h.send(CommandKind::step);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::history_limit);
    auto history = h.session.history();
    EXPECT_EQ(history->publication.state, InteractiveState::failed);
    EXPECT_EQ(history->publication.next_tick, 2);
    EXPECT_EQ(history->result.trace.size(), 2);
    EXPECT_EQ(history->publication.discarded_snapshots, 0);
}
TEST(Interactive, EventAndCommandBudgetsRejectExplicitly) {
    runtime::SessionLimits limits; limits.events = 1;
    Harness h(limits); h.send(CommandKind::start);
    EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::history_limit);
    EXPECT_FALSE(h.session.publication()->latest);
    limits.events = 100; limits.commands = 1;
    Harness other(limits); other.send(CommandKind::start);
    EXPECT_EQ(other.send(CommandKind::step).code, CommandCode::applied);
    EXPECT_EQ(other.send(CommandKind::pause).code, CommandCode::history_limit);
    other.session.close();
    EXPECT_EQ(other.session.publication()->state, InteractiveState::closed);
}
TEST(Interactive, FullReplayMatchesCoreAndConsecutiveSessions) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full;
    Harness a(limits), b(limits);
    for (auto* h : {&a, &b}) {
        h->send(CommandKind::start);
        for (int i = 0; i < 8; ++i) { h->send(CommandKind::step); }
    }
    auto expected = core::simulate(core::conveyor_scenario());
    const auto actual = a.session.history();
    expected.control = actual->result.control;
    EXPECT_FALSE(core::compare_runs(expected, actual->result));
    EXPECT_FALSE(core::compare_runs(actual->result, b.session.history()->result));
}
TEST(Interactive, RecordedFunctionalCommandsReproduceTypedSignals) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full;
    Harness a(limits), b(limits); a.send(CommandKind::start); b.send(CommandKind::start);
    for (core::Tick tick = 0; tick < 4; ++tick) {
        auto input = a.input(tick, tick == 1 ? core::DigitalState::FAULT :
                                  tick == 2 ? core::DigitalState::UNDEFINED : core::DigitalState::HIGH,
                             core::ExternalTarget::sensor_power);
        a.send(CommandKind::step); EXPECT_EQ(input.completion.get().code, CommandCode::applied);
    }
    const auto recorded = a.session.history();
    for (const auto& record : recorded->commands) {
        if (record.command.kind == CommandKind::start) { continue; }
        auto ticket = b.session.submit(record.command);
        if (record.command.kind != CommandKind::input) { EXPECT_EQ(ticket.completion.get().code, CommandCode::applied); }
    }
    EXPECT_FALSE(core::compare_runs(a.session.history()->result, b.session.history()->result));
}
TEST(Interactive, ConcurrentConsumersSeeCoherentMonotonicPublications) {
    auto spec = core::conveyor_scenario(); spec.ticks = 100;
    Harness h({}, spec); h.send(CommandKind::start);
    std::atomic<bool> done{}, coherent{true};
    auto consume = [&] {
        std::uint64_t sequence = 0;
        while (!done.load()) {
            auto p = h.session.publication();
            if (p->sequence < sequence || (p->latest && (p->latest->tick + 1 != p->next_tick ||
                p->latest->time_us != p->next_tick * spec.tick_period_us))) { coherent.store(false); }
            sequence = p->sequence;
            std::this_thread::yield();
        }
    };
    std::jthread one(consume), two(consume);
    for (int i = 0; i < 100; ++i) { EXPECT_EQ(h.send(CommandKind::step).code, CommandCode::applied); }
    done.store(true); one.join(); two.join(); EXPECT_TRUE(coherent.load());
}
TEST(Interactive, ConfigurationAndLimitsAreValidatedBeforeAdmission) {
    auto runner = std::make_shared<runtime::Runner>();
    EXPECT_THROW(runtime::InteractiveSession(runner, 1, core::demo_scenario()), std::invalid_argument);
    auto limits = runtime::SessionLimits{}; limits.snapshots = 0;
    EXPECT_THROW(runtime::InteractiveSession(runner, 1, core::conveyor_scenario(), limits), std::invalid_argument);
    EXPECT_THROW(runtime::InteractiveSession(runner, 0, core::conveyor_scenario()), std::invalid_argument);
}
TEST(Interactive, SessionDestructionSettlesAcceptedFutures) {
    std::shared_future<runtime::CommandReply> pending;
    std::shared_ptr<const runtime::SessionPublication> snapshot;
    {
        Harness h; h.send(CommandKind::start); h.send(CommandKind::step); h.send(CommandKind::pause);
        snapshot = h.session.publication(); pending = h.input(1, core::DigitalState::LOW).completion;
    }
    ASSERT_EQ(pending.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(pending.get().code, CommandCode::cancelled);
    EXPECT_EQ(snapshot->latest->tick, 0);
}
TEST(Interactive, CloseBeforeRunnerStartsDoesNotWaitForAnotherPausedSession) {
    auto runner = std::make_shared<runtime::Runner>(8);
    runtime::InteractiveSession active(runner, 1, core::conveyor_scenario());
    active.submit({1, 1, CommandKind::start}).completion.get();
    active.submit({1, 2, CommandKind::pause}).completion.get();
    runtime::InteractiveSession queued(runner, 2, core::conveyor_scenario());
    auto pending = queued.submit({2, 1, CommandKind::start});
    queued.close();
    const auto outcome = pending.completion.get().code;
    EXPECT_TRUE(outcome == CommandCode::cancelled || outcome == CommandCode::applied);
    EXPECT_EQ(active.publication()->state, InteractiveState::paused);
    EXPECT_EQ(queued.publication()->state, InteractiveState::closed);
}
TEST(Interactive, TonCtuAndPreviousImageMatchCoreWithControls) {
    auto spec = core::conveyor_scenario();
    spec.program.output_count = 3;
    spec.program.rungs.front().ton_us = 1000;
    spec.program.rungs.push_back({{core::Branch{{core::Bank::input, 0, false}}}, 1, {},
                                 core::CounterConfig{1, {core::Bank::input, 1, false}}});
    spec.program.rungs.push_back({{core::Branch{core::Contact::compare({{1}, core::ComparisonOp::greater_equal, 1})}}, 2, {}, {}});
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full;
    Harness h(limits, spec); core::ConveyorSession expected(spec);
    h.send(CommandKind::start);
    for (int i = 0; i < 3; ++i) { h.send(CommandKind::step); expected.step(); }
    h.send(CommandKind::pause); expected.pause();
    h.send(CommandKind::resume); expected.resume();
    h.send(CommandKind::step); expected.step();
    h.send(CommandKind::stop); expected.stop();
    auto reference = expected.result();
    reference.control = core::SessionStatus{expected.state(), expected.next_tick(), expected.position_um(), expected.motor_state()};
    EXPECT_FALSE(core::compare_runs(reference, h.session.history()->result));
}
TEST(Interactive, BoundaryValidationIsAtomicAndDrainingRetainsFunctionalState) {
    auto spec = core::conveyor_scenario();
    core::ConveyorSession session(spec, false);
    const std::array<core::FunctionalEvent, 2> invalid{{{0, 0, core::ExternalTarget::command, core::DigitalState::HIGH},
        {1, 1, core::ExternalTarget::command, core::DigitalState::LOW}}};
    EXPECT_THROW(session.step(invalid), std::invalid_argument);
    EXPECT_EQ(session.next_tick(), 0);
    EXPECT_TRUE(session.result().events.empty());
    core::RunResult combined;
    for (int i = 0; i < 8; ++i) {
        session.step(); auto batch = session.drain_result();
        combined.trace.insert(combined.trace.end(), batch.trace.begin(), batch.trace.end());
        combined.events.insert(combined.events.end(), batch.events.begin(), batch.events.end());
        combined.counter_events.insert(combined.counter_events.end(), batch.counter_events.begin(), batch.counter_events.end());
        EXPECT_TRUE(session.result().trace.empty());
    }
    EXPECT_EQ(combined, core::simulate(spec));
}
TEST(Interactive, DeferredInputAcrossPauseReplaysInAcceptanceOrder) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full;
    Harness a(limits), b(limits);
    a.send(CommandKind::start);
    auto input = a.input(0, core::DigitalState::HIGH);
    a.send(CommandKind::pause); a.send(CommandKind::resume); a.send(CommandKind::step);
    EXPECT_EQ(input.completion.get().code, CommandCode::applied);
    auto commands = a.session.history()->commands;
    std::sort(commands.begin(), commands.end(), [](const auto& left, const auto& right) { return left.command.id < right.command.id; });
    for (const auto& record : commands) {
        const auto ticket = b.session.submit(record.command);
        if (record.command.kind != CommandKind::input) { EXPECT_EQ(ticket.completion.get().code, CommandCode::applied); }
    }
    EXPECT_FALSE(core::compare_runs(a.session.history()->result, b.session.history()->result));
}
