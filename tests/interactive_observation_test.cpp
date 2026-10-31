#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/core/replay.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <tuple>
using namespace sim;
using namespace std::chrono_literals;
namespace {
runtime::CommandReply wait(const runtime::CommandTicket& ticket) {
    if (ticket.completion.wait_for(30s) != std::future_status::ready) { throw std::runtime_error("Timeout"); }
    return ticket.completion.get();
}
runtime::SessionCommand advance(std::uint64_t id, core::Tick ticks) {
    runtime::SessionCommand command{1, id, runtime::CommandKind::advance}; command.ticks = ticks; return command;
}
core::RunSpec spec(core::Tick ticks = 1000) {
    auto value = core::conveyor_scenario(); value.ticks = ticks;
    value.program.output_count = 2; value.program.input_count = 3;
    value.program.rungs.push_back({{core::Branch{{core::Bank::input, 0, false}}}, 1, {},
        core::CounterConfig{3, {core::Bank::input, 2, false}}});
    value.conveyor->events.clear();
    for (core::Tick tick = 0; tick < ticks; ++tick) {
        value.conveyor->events.push_back({tick, tick, core::ExternalTarget::command,
            tick % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    return value;
}
bool coherent(const runtime::SessionObservation& observation) {
    const auto& p = observation.publication;
    if (observation.control && observation.control->next_tick != p.next_tick) { return false; }
    if (p.latest && p.latest->tick + 1 != p.next_tick) { return false; }
    std::uint64_t sequence = observation.events->first_sequence;
    for (const auto& block : observation.events->blocks) {
        for (const auto& e : *block) {
            if (e.sequence != sequence++ || e.publication_sequence > p.sequence || e.tick > p.next_tick) { return false; }
        }
    }
    return sequence == observation.events->next_sequence;
}
}
TEST(InteractiveObservation, ConcurrentReadersSeeOneBoundary) {
    runtime::InteractiveSession session(1, spec(10000));
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    std::atomic<bool> done{}, valid{true};
    const auto read = [&] {
        std::uint64_t sequence = 0;
        while (!done.load()) {
            const auto value = session.observation();
            if (!coherent(*value) || value->publication.sequence < sequence) { valid.store(false); }
            sequence = value->publication.sequence;
        }
    };
    std::jthread a(read), b(read);
    EXPECT_EQ(wait(session.submit(advance(2, 10000))).code, runtime::CommandCode::applied);
    done.store(true); a.join(); b.join();
    EXPECT_TRUE(valid.load());
    EXPECT_EQ(session.observation()->publication.state, runtime::InteractiveState::completed);
}
TEST(InteractiveObservation, ControlsPublishNewRevisionWithoutInventingSnapshot) {
    runtime::InteractiveSession session(1, spec());
    const auto loaded = session.observation();
    EXPECT_FALSE(loaded->publication.latest); EXPECT_EQ(loaded->publication.sequence, 0u);
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit(advance(2, 3)));
    auto previous = session.observation();
    for (const auto kind : {runtime::CommandKind::pause, runtime::CommandKind::resume, runtime::CommandKind::stop}) {
        const auto id = previous->publication.sequence + 10;
        wait(session.submit({1, id, kind}));
        const auto current = session.observation();
        EXPECT_GT(current->publication.sequence, previous->publication.sequence);
        EXPECT_EQ(current->publication.next_tick, 3u);
        EXPECT_EQ(current->publication.latest, previous->publication.latest);
        EXPECT_TRUE(coherent(*current));
        previous = current;
    }
    EXPECT_EQ(previous->control->motor, core::DigitalState::LOW);
    EXPECT_EQ(previous->publication.state, runtime::InteractiveState::stopped);
    session.close();
    const auto closed = session.observation();
    EXPECT_EQ(closed->publication.latest, previous->publication.latest);
    EXPECT_EQ(closed->publication.state, runtime::InteractiveState::closed);
    EXPECT_GT(closed->publication.sequence, previous->publication.sequence);
}
TEST(InteractiveObservation, SameTickUsesTimePhaseAndCounterBeforeCoil) {
    runtime::InteractiveSession session(1, spec(8));
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit(advance(2, 8)));
    const auto observation = session.observation();
    const auto page = runtime::observation_events(*observation, {1, 0}, 8192);
    bool counter_seen = false, output_after_counter = false;
    for (std::size_t i = 0; i < page.events.size(); ++i) {
        const auto& e = page.events[i];
        if (std::holds_alternative<core::CounterEvent>(e.value)) { counter_seen = true; }
        if (i > 0) {
            const auto& before = page.events[i - 1];
            EXPECT_LT(std::tie(before.publication_sequence, before.time_us, before.tick, before.phase, before.phase_order, before.sequence),
                      std::tie(e.publication_sequence, e.time_us, e.tick, e.phase, e.phase_order, e.sequence));
            if (std::holds_alternative<core::CounterEvent>(before.value) && e.phase == core::Phase::ladder &&
                before.tick == e.tick && std::holds_alternative<core::Transition>(e.value)) { output_after_counter = true; }
        }
    }
    EXPECT_TRUE(counter_seen); EXPECT_TRUE(output_after_counter);
    const auto history = session.history();
    EXPECT_EQ(page.events.size(), history->result.events.size() + history->result.counter_events.size());
    EXPECT_EQ(page.lost, 0u);
}
TEST(InteractiveObservation, SlowConsumerDetectsWindowLossWithoutLosingDurableEvents) {
    runtime::SessionLimits limits; limits.snapshots = 2;
    runtime::InteractiveSession session(1, spec(5000), limits);
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit(advance(2, 2)));
    auto held = session.observation();
    const auto retained_end = held->events->next_sequence;
    wait(session.submit(advance(3, 4998)));
    const auto now = session.observation();
    EXPECT_EQ(held->events->next_sequence, retained_end);
    EXPECT_EQ(held->publication.next_tick, 2u);
    EXPECT_LE(now->events->blocks.size(), runtime::observation_storage_blocks);
    const auto page = runtime::observation_events(*now, {1, retained_end}, 1);
    EXPECT_GT(page.lost, 0u);
    ASSERT_EQ(page.events.size(), 1u);
    EXPECT_EQ(page.events.front().sequence, now->events->first_sequence);
    const auto history = session.history();
    EXPECT_EQ(now->events->next_sequence, history->result.events.size() + history->result.counter_events.size());
    EXPECT_EQ(history->result.trace.size(), 2u);
    auto cursor = page.next;
    while (cursor.next_sequence < now->events->next_sequence) {
        const auto next = runtime::observation_events(*now, cursor, 7);
        ASSERT_FALSE(next.events.empty()); EXPECT_EQ(next.lost, 0u); cursor = next.next;
    }
    EXPECT_TRUE(runtime::observation_events(*now, cursor).events.empty());
}
TEST(InteractiveObservation, RetainedObservationSurvivesCloseAndNewSession) {
    std::shared_ptr<const runtime::SessionObservation> held;
    {
        runtime::InteractiveSession session(1, spec());
        wait(session.submit({1, 1, runtime::CommandKind::start}));
        wait(session.submit(advance(2, 2)));
        session.close(); held = session.observation();
    }
    runtime::InteractiveSession another(2, spec());
    EXPECT_TRUE(coherent(*held));
    EXPECT_EQ(held->publication.state, runtime::InteractiveState::closed);
    EXPECT_THROW(runtime::observation_events(*another.observation(), {1, 0}), std::invalid_argument);
    EXPECT_THROW(runtime::observation_events(*held, {1, held->events->next_sequence + 1}), std::invalid_argument);
    EXPECT_THROW(runtime::observation_events(*held, {1, 0}, 0), std::invalid_argument);
    EXPECT_THROW(runtime::observation_events(*held, {1, 0}, 8193), std::invalid_argument);
}
TEST(InteractiveObservation, FatalCauseRemainsOnSameBoundaryAfterClose) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 1;
    runtime::InteractiveSession session(1, spec(5000), limits);
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    EXPECT_EQ(wait(session.submit(advance(2, 2))).code, runtime::CommandCode::history_limit);
    const auto failed = session.observation();
    ASSERT_TRUE(failed->publication.diagnostic);
    EXPECT_EQ(failed->publication.state, runtime::InteractiveState::failed);
    EXPECT_TRUE(coherent(*failed));
    session.close();
    const auto closed = session.observation();
    EXPECT_EQ(closed->publication.diagnostic, failed->publication.diagnostic);
    EXPECT_EQ(closed->publication.latest, failed->publication.latest);
    EXPECT_EQ(closed->events, failed->events);
}
TEST(InteractiveObservation, QueryReusesPublishedObjectAndReportsTiming) {
    runtime::InteractiveSession session(1, spec());
    const auto held = session.observation();
    constexpr int queries = 100000;
    const auto begin = std::chrono::steady_clock::now();
    bool identical = true;
    for (int i = 0; i < queries; ++i) { identical = identical && session.observation() == held; }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
    EXPECT_TRUE(identical);
    RecordProperty("observation_mean_ns", ns / queries);
    std::cout << "observation(): " << ns / queries << " ns/query (idle, no real-time guarantee)\n";
}
TEST(InteractiveObservation, OneLargeBoundaryStillHasBoundedWindowAndExplicitLoss) {
    auto value = spec(2); value.conveyor->events.clear();
    for (std::uint64_t i = 0; i < 9000; ++i) {
        value.conveyor->events.push_back({0, i, core::ExternalTarget::command,
            i % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    runtime::InteractiveSession session(1, value);
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit(advance(2, 1)));
    const auto observation = session.observation();
    const auto page = runtime::observation_events(*observation, {1, 0}, 8192);
    EXPECT_LE(observation->events->blocks.size(), runtime::observation_storage_blocks);
    EXPECT_LE(page.events.size(), 8192u);
    EXPECT_GT(page.lost, 0u);
    EXPECT_EQ(page.lost + page.events.size(), observation->events->next_sequence);
    EXPECT_TRUE(coherent(*observation));
    const auto history = session.history();
    EXPECT_EQ(observation->events->next_sequence, history->result.events.size() + history->result.counter_events.size());
}
TEST(InteractiveObservation, WindowMeasurementSameScenario) {
    for (int round = 0; round < 3; ++round) {
        runtime::InteractiveSession session(1, spec(10000));
        wait(session.submit({1, 1, runtime::CommandKind::start}));
        const auto begin = std::chrono::steady_clock::now();
        EXPECT_EQ(wait(session.submit(advance(2, 10000))).code, runtime::CommandCode::applied);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
        const auto value = session.observation();
        std::size_t slots = 0;
        for (const auto& block : value->events->blocks) { slots += block->capacity(); }
        std::cout << "window measurement: ns/tick=" << ns / 10000
                  << " total=" << value->events->next_sequence
                  << " retained=" << value->events->next_sequence - value->events->first_sequence
                  << " blocks=" << value->events->blocks.size()
                  << " vector_payload_bytes=" << slots * sizeof(runtime::ObservationEvent) << '\n';
    }
}
TEST(InteractiveObservation, SingleEventPublicationsFillNominalCapacityBeforeDiscard) {
    runtime::InteractiveSession session(1, spec(2));
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    std::shared_ptr<const runtime::SessionObservation> full;
    for (std::uint64_t n = 1; n <= 8193; ++n) {
        const auto kind = n % 2 ? runtime::CommandKind::pause : runtime::CommandKind::resume;
        ASSERT_EQ(wait(session.submit({1, n + 1, kind})).code, runtime::CommandCode::applied);
        const auto value = session.observation();
        ASSERT_EQ(value->events->next_sequence, n);
        ASSERT_EQ(value->events->first_sequence, n > 8192 ? n - 8192 : 0);
        ASSERT_LE(value->events->blocks.size(), runtime::observation_storage_blocks);
        if (n == 8192) { full = value; }
    }
    ASSERT_TRUE(full);
    EXPECT_EQ(full->events->blocks.size(), 32u);
    for (const auto& block : full->events->blocks) { EXPECT_EQ(block->size(), 256u); }
    const auto now = session.observation();
    const auto page = runtime::observation_events(*now, {1, 0}, 8192);
    ASSERT_EQ(page.events.size(), 8192u); EXPECT_EQ(page.lost, 1u);
    EXPECT_EQ(page.events.front().sequence, 1u); EXPECT_EQ(page.events.back().sequence, 8192u);
    EXPECT_EQ(full->events->blocks.front()->front().sequence, 0u);
    const auto history = session.history();
    EXPECT_EQ(history->result.events.size(), 8193u);
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
}
TEST(InteractiveObservation, PartialTailIsCopiedAndSealedBlocksAreShared) {
    runtime::InteractiveSession session(1, spec(2));
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit({1, 2, runtime::CommandKind::pause}));
    const auto old = session.observation();
    wait(session.submit({1, 3, runtime::CommandKind::resume}));
    const auto next = session.observation();
    ASSERT_EQ(old->events->blocks.size(), 1u); EXPECT_EQ(old->events->blocks[0]->size(), 1u);
    ASSERT_EQ(next->events->blocks.size(), 1u); EXPECT_EQ(next->events->blocks[0]->size(), 2u);
    EXPECT_NE(old->events->blocks[0], next->events->blocks[0]);
    for (std::uint64_t n = 3; n <= 256; ++n) {
        wait(session.submit({1, n + 1, n % 2 ? runtime::CommandKind::pause : runtime::CommandKind::resume}));
    }
    const auto sealed = session.observation();
    wait(session.submit({1, 258, runtime::CommandKind::pause}));
    const auto appended = session.observation();
    ASSERT_EQ(appended->events->blocks.size(), 2u);
    EXPECT_EQ(appended->events->blocks.front(), sealed->events->blocks.front());
    EXPECT_EQ(appended->events->blocks.back()->size(), 1u);
    EXPECT_TRUE(coherent(*old)); EXPECT_TRUE(coherent(*appended));
}
TEST(InteractiveObservation, ConcurrentConsumerRetainsOldPackedWindowAndReplay) {
    runtime::InteractiveSession session(1, spec(10000));
    wait(session.submit({1, 1, runtime::CommandKind::start}));
    wait(session.submit(advance(2, 100)));
    const auto held = session.observation();
    const auto before = runtime::observation_events(*held, {1, 0}, 8192);
    std::atomic<bool> done{}, valid{true};
    std::jthread reader([&] {
        while (!done.load()) {
            const auto old = runtime::observation_events(*held, {1, 0}, 8192);
            if (!coherent(*held) || old.events.size() != before.events.size() ||
                !coherent(*session.observation())) { valid.store(false); }
        }
    });
    EXPECT_EQ(wait(session.submit(advance(3, 9900))).code, runtime::CommandCode::applied);
    done.store(true); reader.join(); EXPECT_TRUE(valid.load());
    const auto now = session.observation();
    EXPECT_EQ(now->events->next_sequence - now->events->first_sequence, 8192u);
    const auto page = runtime::observation_events(*now, {1, 0}, 8192);
    for (std::size_t i = 1; i < page.events.size(); ++i) {
        const auto& a = page.events[i-1]; const auto& b = page.events[i];
        EXPECT_LT(std::tie(a.publication_sequence, a.time_us, a.tick, a.phase, a.phase_order, a.sequence),
                  std::tie(b.publication_sequence, b.time_us, b.tick, b.phase, b.phase_order, b.sequence));
    }
    const auto history = session.history();
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
    EXPECT_EQ(held->publication.next_tick, 100u);
}
