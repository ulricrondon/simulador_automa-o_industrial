#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/core/replay.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
using namespace sim;
using namespace std::chrono_literals;
namespace {
using runtime::CommandCode;
using runtime::CommandKind;
using runtime::HistoryMode;
using runtime::InteractiveSession;
using runtime::InteractiveState;
runtime::CommandReply finish(const runtime::CommandTicket& ticket) {
    if (ticket.completion.wait_for(60s) != std::future_status::ready) { throw std::runtime_error("Session request timeout"); }
    return ticket.completion.get();
}
runtime::SessionCommand advance(std::uint64_t id, core::Tick ticks) {
    runtime::SessionCommand command{1, id, CommandKind::advance}; command.ticks = ticks; return command;
}
core::RunSpec counting_spec(core::Tick ticks) {
    auto spec = core::conveyor_scenario();
    spec.ticks = ticks;
    spec.program.input_count = 4; spec.program.output_count = 4;
    spec.program.rungs[0].ton_us = 2000;
    spec.program.rungs.push_back({{core::Branch{{core::Bank::input, 1, false}}}, 1, {}, core::CounterConfig{1, {core::Bank::input, 0, true}}});
    return spec;
}
// Every tick changes the command, so records depend on the whole configured list.
core::RunSpec toggling_spec(core::Tick ticks) {
    auto spec = core::conveyor_scenario();
    spec.ticks = ticks; spec.conveyor->events.clear();
    for (core::Tick tick = 0; tick < ticks; ++tick) {
        spec.conveyor->events.push_back({tick, tick, core::ExternalTarget::command, tick % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    return spec;
}
void expect_consecutive(const std::vector<std::shared_ptr<const core::Snapshot>>& trace) {
    for (std::size_t i = 1; i < trace.size(); ++i) { ASSERT_EQ(trace[i]->tick, trace[i - 1]->tick + 1); }
}
std::size_t retained(const runtime::SessionHistoryView& view) { return view.events.size + view.counter_events.size; }
}
TEST(InteractiveHistory, ConcurrentHistoryAndPublicationStayCoherentWhileAdvancing) {
    const auto spec = counting_spec(20000);
    runtime::SessionLimits limits; limits.history = HistoryMode::full; limits.snapshots = 20000;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    std::atomic<bool> done{}, coherent{true};
    std::jthread publications([&] {
        std::uint64_t sequence = 0;
        while (!done.load()) {
            const auto p = session.publication();
            if (p->sequence < sequence || (p->latest && p->latest->tick + 1 != p->next_tick)) { coherent.store(false); }
            sequence = p->sequence;
            std::this_thread::yield();
        }
    });
    std::jthread views([&] {
        while (!done.load()) {
            const auto view = session.history_view();
            const auto last = view->publication.last_tick;
            if (view->trace.size() != view->publication.next_tick || (last && view->trace.back()->tick != *last) ||
                (view->trace.empty() ? false : view->trace.front()->tick != 0)) { coherent.store(false); }
            std::this_thread::yield();
        }
    });
    std::jthread copies([&] {
        for (int i = 0; i < 5 && !done.load(); ++i) {
            const auto history = session.history();
            if (history->result.trace.size() != history->publication.next_tick) { coherent.store(false); }
        }
    });
    EXPECT_EQ(finish(session.submit(advance(2, spec.ticks))).code, CommandCode::applied);
    done.store(true); publications.join(); views.join(); copies.join();
    EXPECT_TRUE(coherent.load());
}
TEST(InteractiveHistory, FullHistorySharesManySnapshotsAndMatchesBatch) {
    const auto spec = counting_spec(20000);
    runtime::SessionLimits limits; limits.history = HistoryMode::full; limits.snapshots = 20000;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    finish(session.submit(advance(2, spec.ticks)));
    const auto first = session.history_view(), second = session.history_view();
    ASSERT_EQ(first->trace.size(), 20000u);
    expect_consecutive(first->trace);
    EXPECT_EQ(first->trace.back(), session.publication()->latest); // Same object, no second copy.
    EXPECT_EQ(first->trace.front(), second->trace.front());
    auto expected = core::simulate(spec);
    const auto history = runtime::materialize(*first);
    expected.control = history.result.control;
    EXPECT_FALSE(core::compare_runs(expected, history.result));
    EXPECT_FALSE(core::compare_runs(history.result, session.history()->result));
    EXPECT_FALSE(core::compare_runs(history.result, runtime::replay_session(*history.replay)));
}
TEST(InteractiveHistory, BoundedHistoryKeepsWindowAndEveryFunctionalEvent) {
    const auto spec = toggling_spec(9000);
    runtime::SessionLimits limits; limits.snapshots = 16;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    finish(session.submit(advance(2, spec.ticks)));
    const auto view = session.history_view();
    ASSERT_EQ(view->trace.size(), 16u);
    expect_consecutive(view->trace);
    EXPECT_EQ(view->trace.front()->tick, core::Tick{9000 - 16});
    EXPECT_EQ(view->publication.discarded_snapshots, 9000u - 16u);
    const auto expected = core::simulate(spec);
    EXPECT_GT(view->events.size, 4096u); // Crosses shared block boundaries.
    EXPECT_EQ(view->events.flatten(), expected.events);
    EXPECT_EQ(view->counter_events.flatten(), expected.counter_events);
    const auto history = runtime::materialize(*view);
    EXPECT_FALSE(core::compare_runs(history.result, runtime::replay_session(*history.replay)));
}
TEST(InteractiveHistory, SlowConsumerPinsOnlyItsOwnViewAndDoesNotDelayTheSession) {
    const auto spec = counting_spec(2000);
    runtime::SessionLimits limits; limits.snapshots = 8;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    finish(session.submit(advance(2, 8)));
    auto held = session.history_view();
    const std::weak_ptr<const core::Snapshot> oldest = held->trace.front();
    const auto copy = *held->trace.front();
    EXPECT_EQ(finish(session.submit(advance(3, 1000))).code, CommandCode::applied);
    const auto now = session.history_view();
    EXPECT_EQ(now->trace.size(), 8u);
    EXPECT_EQ(now->trace.front()->tick, core::Tick{1000});
    ASSERT_FALSE(oldest.expired());
    EXPECT_EQ(*oldest.lock(), copy);
    EXPECT_EQ(held->publication.next_tick, core::Tick{8}); // The view does not change afterwards.
    held.reset();
    EXPECT_TRUE(oldest.expired());
}
TEST(InteractiveHistory, SnapshotPagesAreBoundedAndReportEvictedRanges) {
    const auto spec = counting_spec(100);
    runtime::SessionLimits limits; limits.snapshots = 10;
    InteractiveSession session(1, spec, limits);
    EXPECT_TRUE(session.snapshots(0, 4).snapshots.empty());
    EXPECT_FALSE(session.snapshots(0, 4).first_tick);
    finish(session.submit({1, 1, CommandKind::start}));
    finish(session.submit(advance(2, 30)));
    const auto evicted = session.snapshots(0, 4);
    EXPECT_EQ(evicted.first_tick, core::Tick{20});
    EXPECT_EQ(evicted.last_tick, core::Tick{29});
    EXPECT_EQ(evicted.discarded_snapshots, 20u);
    ASSERT_EQ(evicted.snapshots.size(), 4u);
    EXPECT_EQ(evicted.snapshots.front()->tick, core::Tick{20});
    expect_consecutive(evicted.snapshots);
    const auto tail = session.snapshots(28, 4);
    ASSERT_EQ(tail.snapshots.size(), 2u);
    EXPECT_EQ(tail.snapshots.back(), session.publication()->latest);
    EXPECT_TRUE(session.snapshots(30, 4).snapshots.empty());
    EXPECT_THROW((void)session.snapshots(0, 0), std::invalid_argument);
    EXPECT_THROW((void)session.snapshots(0, runtime::max_snapshot_page + 1), std::invalid_argument);
}
TEST(InteractiveHistory, CloseDuringQueriesKeepsViewsValidAfterDestruction) {
    for (int round = 0; round < 20; ++round) {
        const auto spec = counting_spec(100000);
        std::shared_ptr<const runtime::SessionHistoryView> kept;
        std::shared_ptr<const runtime::SessionHistory> copied;
        runtime::CommandTicket batch;
        {
            InteractiveSession session(1, spec);
            finish(session.submit({1, 1, CommandKind::start}));
            batch = session.submit(advance(2, spec.ticks));
            std::atomic<bool> done{};
            std::atomic<bool> viewing{}, copying{};
            std::jthread readers([&] {
                while (!done.load()) {
                    (void)session.publication();
                    (void)session.snapshots(0, 16);
                    kept = session.history_view();
                    viewing.store(true);
                    std::this_thread::yield();
                }
            });
            std::jthread copier([&] {
                while (!done.load()) {
                    copied = session.history();
                    copying.store(true);
                    std::this_thread::yield();
                }
            });
            while (!viewing.load() || !copying.load()) { std::this_thread::yield(); }
            const auto closed = session.request_close();
            const auto settled = closed.wait_for(10s);
            done.store(true); readers.join(); copier.join();
            ASSERT_EQ(settled, std::future_status::ready);
            kept = session.history_view();
            EXPECT_EQ(kept->publication.state, InteractiveState::closed);
        }
        EXPECT_EQ(finish(batch).code, CommandCode::cancelled);
        ASSERT_TRUE(copied);
        expect_consecutive(kept->trace);
        const auto history = runtime::materialize(*kept);
        EXPECT_EQ(finish(batch).advanced_ticks, history.publication.next_tick);
        EXPECT_FALSE(core::compare_runs(history.result, runtime::replay_session(*history.replay)));
    }
}
TEST(InteractiveHistory, ManyDistributedConfiguredEventsFitWhenOnlyOneBelongsToEachTick) {
    auto spec = core::conveyor_scenario();
    spec.ticks = 100000; spec.conveyor->events.clear();
    for (core::Tick tick = 0; tick < spec.ticks; ++tick) {
        spec.conveyor->events.push_back({tick, spec.ticks - tick, core::ExternalTarget::command, core::DigitalState::LOW});
    }
    std::reverse(spec.conveyor->events.begin(), spec.conveyor->events.end());
    InteractiveSession session(1, spec);
    finish(session.submit({1, 1, CommandKind::start}));
    const auto reply = finish(session.submit(advance(2, spec.ticks)));
    EXPECT_EQ(reply.code, CommandCode::applied);
    EXPECT_EQ(reply.advanced_ticks, spec.ticks);
    const auto view = session.history_view();
    EXPECT_EQ(view->publication.state, InteractiveState::completed);
    const auto expected = core::simulate(spec);
    EXPECT_EQ(view->events.flatten(), expected.events);
    EXPECT_EQ(view->publication.latest->tick, expected.trace.back().tick);
    EXPECT_EQ(*view->publication.latest, expected.trace.back());
}
TEST(InteractiveHistory, RealEventLimitFailsExplicitlyWithoutDroppingRecords) {
    const auto spec = toggling_spec(2000);
    const auto expected = core::simulate(spec);
    ASSERT_GT(expected.events.size(), 500u);
    runtime::SessionLimits limits; limits.events = 500;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    const auto reply = finish(session.submit(advance(2, spec.ticks)));
    EXPECT_EQ(reply.code, CommandCode::history_limit);
    EXPECT_LT(reply.advanced_ticks, spec.ticks);
    const auto view = session.history_view();
    EXPECT_EQ(view->publication.state, InteractiveState::failed);
    EXPECT_EQ(view->publication.error, CommandCode::history_limit);
    EXPECT_LE(retained(*view), limits.events);
    // The retained records are an exact prefix of the batch run: nothing skipped to fit.
    const auto events = view->events.flatten();
    ASSERT_LE(events.size(), expected.events.size());
    EXPECT_TRUE(std::equal(events.begin(), events.end(), expected.events.begin()));
    EXPECT_EQ(finish(session.submit({1, 3, CommandKind::stop})).code, CommandCode::applied);
    const auto history = session.history();
    EXPECT_EQ(history->result.control->motor, core::DigitalState::LOW);
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveHistory, StepBudgetBoundIsNeverExceededForAnyEventLimit) {
    auto spec = toggling_spec(120);
    spec.program.input_count = 4; spec.program.output_count = 8;
    spec.program.rungs.push_back({{core::Branch{{core::Bank::input, 0, false}}}, 1, std::uint64_t{1500}});
    spec.program.rungs.push_back({{core::Branch{{core::Bank::input, 0, false}}}, 2, {}, core::CounterConfig{2, {core::Bank::input, 1, false}}});
    spec.program.rungs.push_back({{core::Branch{{core::Bank::input, 1, false}}}, 3, {}, core::CounterConfig{1, {core::Bank::input, 0, true}}});
    const auto expected = core::simulate(spec);
    const auto total = expected.events.size() + expected.counter_events.size();
    bool completed{};
    for (std::size_t budget = 1; budget <= total + 16; budget += 3) {
        runtime::SessionLimits limits; limits.events = budget;
        InteractiveSession session(1, spec, limits);
        finish(session.submit({1, 1, CommandKind::start}));
        const auto reply = finish(session.submit(advance(2, spec.ticks)));
        const auto view = session.history_view();
        ASSERT_LE(retained(*view), budget);
        const auto events = view->events.flatten();
        ASSERT_TRUE(std::equal(events.begin(), events.end(), expected.events.begin()));
        if (reply.code == CommandCode::applied) { completed = true; EXPECT_EQ(events, expected.events); }
    }
    EXPECT_TRUE(completed); // A budget matching the real records must be enough.
}
TEST(InteractiveHistory, GroupingOfTicksAndPausesNeverChangesTheFunctionalResult) {
    const auto spec = counting_spec(200);
    auto batch_spec = spec;
    std::vector<runtime::SessionCommand> inputs;
    std::uint64_t sequence = 1000;
    for (const auto tick : {core::Tick{1}, core::Tick{9}, core::Tick{17}, core::Tick{18}, core::Tick{60}, core::Tick{61}}) {
        const auto value = tick == 9 ? core::DigitalState::LOW : tick == 17 ? core::DigitalState::FAULT : core::DigitalState::HIGH;
        const auto target = tick == 17 || tick == 18 ? core::ExternalTarget::motor_power : core::ExternalTarget::command;
        batch_spec.conveyor->events.push_back({tick, sequence++, target, value});
        inputs.push_back({1, 0, CommandKind::input, tick, target, value});
    }
    const auto expected = core::simulate(batch_spec);
    runtime::SessionLimits limits; limits.history = HistoryMode::full;
    const std::vector<std::vector<core::Tick>> groupings{std::vector<core::Tick>(200, 1), {200}, {7, 3, 50, 1, 139}, {1, 199}};
    for (const auto& groups : groupings) {
        for (const bool pausing : {false, true}) {
            InteractiveSession session(1, spec, limits);
            std::uint64_t id = 0;
            finish(session.submit({1, ++id, CommandKind::start}));
            std::vector<runtime::CommandTicket> tickets;
            for (auto input : inputs) { input.id = ++id; tickets.push_back(session.submit(input)); }
            for (const auto ticks : groups) {
                const auto ticket = session.submit(advance(++id, ticks));
                if (pausing) { finish(session.submit({1, ++id, CommandKind::pause})); finish(session.submit({1, ++id, CommandKind::resume})); }
                EXPECT_EQ(finish(ticket).code, CommandCode::applied);
            }
            for (const auto& ticket : tickets) { EXPECT_EQ(finish(ticket).code, CommandCode::applied); }
            const auto history = session.history();
            EXPECT_EQ(history->result.trace, expected.trace);
            EXPECT_EQ(history->result.counter_events, expected.counter_events);
            if (!pausing) { EXPECT_EQ(history->result.events, expected.events); }
            EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
        }
    }
}
TEST(InteractiveHistory, StopDuringAdvanceWithActiveReadersReplaysExactly) {
    for (int round = 0; round < 20; ++round) {
        const auto spec = counting_spec(100000);
        InteractiveSession session(1, spec);
        finish(session.submit({1, 1, CommandKind::start}));
        const auto batch = session.submit(advance(2, spec.ticks));
        std::atomic<bool> done{};
        std::jthread reader([&] { while (!done.load()) { (void)session.history_view(); std::this_thread::yield(); } });
        while (session.publication()->next_tick < 20) { std::this_thread::yield(); }
        EXPECT_EQ(finish(session.submit({1, 3, CommandKind::stop})).code, CommandCode::applied);
        done.store(true); reader.join();
        const auto cancelled = finish(batch);
        EXPECT_EQ(cancelled.code, CommandCode::cancelled);
        EXPECT_EQ(cancelled.advanced_ticks, session.publication()->next_tick);
        const auto history = session.history();
        EXPECT_EQ(history->publication.state, InteractiveState::stopped);
        EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
    }
}
