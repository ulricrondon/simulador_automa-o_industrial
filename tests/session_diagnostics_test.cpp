#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "sim/core/replay.hpp"
#include "../src/runtime/session_diagnostics.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
using namespace sim;
using namespace std::chrono_literals;
namespace {
using runtime::CommandKind;
using runtime::CommandCode;
using runtime::DiagnosticReason;
using runtime::DiagnosticStage;
using runtime::InteractiveSession;
runtime::CommandReply finish(const runtime::CommandTicket& ticket) {
    if (ticket.completion.wait_for(3s) != std::future_status::ready) { throw std::runtime_error("Command timeout"); }
    return ticket.completion.get();
}
void expect_reason(const runtime::CommandReply& reply, CommandCode code, DiagnosticReason reason) {
    EXPECT_EQ(reply.code, code); ASSERT_TRUE(reply.diagnostic);
    EXPECT_EQ(reply.diagnostic->code, code); EXPECT_EQ(reply.diagnostic->reason, reason);
    EXPECT_FALSE(reply.diagnostic->message.empty());
}
}
TEST(SessionDiagnostics, AdmissionQueueLimitIncludesCapacityAndNoInventedTick) {
    runtime::SessionLimits limits; limits.queue_capacity = 1;
    InteractiveSession session(7, core::conveyor_scenario(), limits);
    const auto batch = session.submit_batch({{7, 1, CommandKind::start}, {7, 2, CommandKind::step}});
    expect_reason(batch[1].acceptance, CommandCode::queue_full, DiagnosticReason::queue_capacity);
    const auto d = finish(batch[1]).diagnostic;
    ASSERT_TRUE(d);
    EXPECT_EQ(d, batch[1].acceptance.diagnostic);
    EXPECT_EQ(d->context.session_id, 7); EXPECT_EQ(d->context.command_id, 2);
    EXPECT_EQ(d->context.command, CommandKind::step);
    EXPECT_EQ(d->context.used, 1); EXPECT_EQ(d->context.limit, 1); EXPECT_EQ(d->context.requested, 1);
    EXPECT_FALSE(d->context.tick); EXPECT_FALSE(d->context.state);
    EXPECT_EQ(d->context.stage, DiagnosticStage::admission);
    EXPECT_FALSE(finish(batch[0]).diagnostic);
}
TEST(SessionDiagnostics, ControlInputAndAdvanceBudgetsAreDistinct) {
    runtime::SessionLimits limits; limits.commands = 1; limits.functional_commands = 1; limits.advance_requests = 1;
    InteractiveSession session(1, core::conveyor_scenario(), limits);
    finish(session.submit({1, 1, CommandKind::start}));
    expect_reason(finish(session.submit({1, 2, CommandKind::pause})), CommandCode::history_limit, DiagnosticReason::control_budget);
    auto input = session.submit({1, 3, CommandKind::input, 0});
    expect_reason(finish(session.submit({1, 4, CommandKind::input, 0})), CommandCode::history_limit, DiagnosticReason::input_budget);
    finish(session.submit({1, 5, CommandKind::step})); finish(input);
    const auto reply = finish(session.submit({1, 6, CommandKind::step}));
    expect_reason(reply, CommandCode::history_limit, DiagnosticReason::advance_budget);
    EXPECT_EQ(reply.diagnostic->context.limit, 1);
    EXPECT_EQ(finish(session.submit({1, 7, CommandKind::stop})).code, CommandCode::applied);
}
TEST(SessionDiagnostics, SnapshotBudgetIsPreservedInFailurePublicationAndHistory) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 1;
    InteractiveSession session(1, core::conveyor_scenario(), limits);
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    auto reply = finish(session.submit({1, 3, CommandKind::step}));
    expect_reason(reply, CommandCode::history_limit, DiagnosticReason::snapshot_budget);
    EXPECT_EQ(reply.diagnostic->context.tick, 1); EXPECT_EQ(reply.diagnostic->context.used, 1);
    EXPECT_EQ(reply.diagnostic->context.limit, 1);
    auto published = session.publication();
    EXPECT_EQ(published->error, CommandCode::history_limit);
    EXPECT_EQ(published->diagnostic, reply.diagnostic);
    EXPECT_EQ(published->next_tick, 1);
    session.close();
    EXPECT_EQ(session.publication()->diagnostic, reply.diagnostic);
    EXPECT_EQ(session.history_view()->commands.flatten().back().reply.diagnostic, reply.diagnostic);
}
TEST(SessionDiagnostics, EventCapacityReportsConservativeRequestedSpace) {
    runtime::SessionLimits limits; limits.events = 1;
    InteractiveSession session(1, core::conveyor_scenario(), limits);
    finish(session.submit({1, 1, CommandKind::start}));
    auto input = session.submit({1, 2, CommandKind::input, 0});
    const auto reply = finish(session.submit({1, 3, CommandKind::step}));
    expect_reason(reply, CommandCode::history_limit, DiagnosticReason::event_budget);
    EXPECT_EQ(reply.diagnostic->context.limit, 1);
    EXPECT_GT(*reply.diagnostic->context.requested, 1);
    expect_reason(finish(input), CommandCode::cancelled, DiagnosticReason::cancelled);
    EXPECT_EQ(session.diagnostic(), reply.diagnostic); // Cleanup cannot hide the root failure.
    EXPECT_FALSE(session.publication()->latest);
}
TEST(SessionDiagnostics, HorizonDistinguishesAdmissionFromRemainingTime) {
    auto spec = core::conveyor_scenario();
    InteractiveSession session(1, spec);
    runtime::SessionCommand request{1, 1, CommandKind::advance}; request.ticks = 100001;
    auto rejected = finish(session.submit(request));
    expect_reason(rejected, CommandCode::invalid, DiagnosticReason::horizon);
    EXPECT_EQ(rejected.diagnostic->context.limit, spec.ticks);
    EXPECT_EQ(rejected.diagnostic->context.requested, 100001);
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    request.id = 3; request.ticks = spec.ticks;
    rejected = finish(session.submit(request));
    expect_reason(rejected, CommandCode::invalid, DiagnosticReason::horizon);
    EXPECT_EQ(rejected.diagnostic->context.stage, DiagnosticStage::application);
    EXPECT_EQ(rejected.diagnostic->context.used, 1); EXPECT_EQ(rejected.diagnostic->context.tick, 1);
    spec.ticks = 100001;
    EXPECT_THROW(InteractiveSession(2, spec), std::invalid_argument);
}
TEST(SessionDiagnostics, InvalidCommandIdentityAndDigitalInputsHaveSpecificReasons) {
    InteractiveSession session(1, core::conveyor_scenario());
    EXPECT_FALSE(session.diagnostic());
    expect_reason(finish(session.submit({99, 1, CommandKind::start})), CommandCode::stale, DiagnosticReason::wrong_session);
    expect_reason(finish(session.submit({1, 0, CommandKind::start})), CommandCode::stale, DiagnosticReason::stale_id);
    expect_reason(finish(session.submit({1, 1, static_cast<CommandKind>(99)})), CommandCode::invalid, DiagnosticReason::invalid_command);
    expect_reason(finish(session.submit({1, 1, CommandKind::input, 0, core::ExternalTarget::command, static_cast<core::DigitalState>(99)})), CommandCode::invalid, DiagnosticReason::invalid_value);
    expect_reason(finish(session.submit({1, 1, CommandKind::input, 0, static_cast<core::ExternalTarget>(99)})), CommandCode::invalid, DiagnosticReason::invalid_target);
    const auto reply = finish(session.submit({1, 1, CommandKind::step}));
    expect_reason(reply, CommandCode::invalid, DiagnosticReason::invalid_state);
    EXPECT_EQ(reply.diagnostic->context.state, runtime::InteractiveState::loaded);
    EXPECT_EQ(finish(session.submit({1, 2, CommandKind::start})).code, CommandCode::applied);
}
TEST(SessionDiagnostics, ClosedSessionAndCancelledInputRetainImmutableDiagnostics) {
    runtime::DiagnosticPtr saved;
    {
        InteractiveSession session(1, core::conveyor_scenario());
        finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::pause}));
        auto input = session.submit({1, 3, CommandKind::input, 0});
        session.close();
        auto reply = finish(input); expect_reason(reply, CommandCode::cancelled, DiagnosticReason::cancelled);
        saved = reply.diagnostic;
        auto closed = finish(session.submit({1, 4, CommandKind::step}));
        expect_reason(closed, CommandCode::closed, DiagnosticReason::session_closed);
        EXPECT_EQ(session.diagnostic(), closed.diagnostic);
    }
    ASSERT_TRUE(saved); EXPECT_EQ(saved->reason, DiagnosticReason::cancelled);
    EXPECT_EQ(saved->context.command_id, 3);
}
TEST(SessionDiagnostics, StaleBoundaryAndConcurrentAdvanceHaveSpecificContext) {
    InteractiveSession session(1, core::conveyor_scenario());
    finish(session.submit({1, 1, CommandKind::start})); finish(session.submit({1, 2, CommandKind::step}));
    const auto stale = finish(session.submit({1, 3, CommandKind::input, 0}));
    expect_reason(stale, CommandCode::stale, DiagnosticReason::stale_boundary);
    EXPECT_EQ(stale.diagnostic->context.tick, 1); EXPECT_EQ(stale.diagnostic->context.requested, 0);
    auto batch = session.submit_batch({{1, 4, CommandKind::step}, {1, 5, CommandKind::step}});
    expect_reason(finish(batch[1]), CommandCode::invalid, DiagnosticReason::advance_active);
    finish(batch[0]);
    finish(session.submit({1, 6, CommandKind::stop}));
    expect_reason(finish(session.submit({1, 7, CommandKind::stop})), CommandCode::invalid, DiagnosticReason::stop_pending);
}
TEST(SessionDiagnostics, KnownRecoverableExceptionsPreserveDomainMessages) {
    runtime::DiagnosticContext context;
    context.session_id = 42; context.command_id = 3; context.tick = 9;
    const auto diagnostic = runtime::detail::exception_diagnostic(
        std::make_exception_ptr(std::invalid_argument("Invalid boundary input")), context);
    EXPECT_EQ(diagnostic->code, CommandCode::execution_error);
    EXPECT_EQ(diagnostic->reason, DiagnosticReason::known_exception);
    EXPECT_EQ(diagnostic->message, "Invalid boundary input");
    EXPECT_EQ(diagnostic->context.session_id, 42); EXPECT_EQ(diagnostic->context.tick, 9);
    EXPECT_EQ(diagnostic->context.stage, DiagnosticStage::execution);
    const auto overflow = runtime::detail::exception_diagnostic(std::make_exception_ptr(std::overflow_error("Time exceeds range")), {});
    EXPECT_EQ(overflow->message, "Time exceeds range");
    // Conversion consumes the exception without poisoning subsequent operations.
    InteractiveSession session(1, core::conveyor_scenario());
    EXPECT_EQ(finish(session.submit({1, 1, CommandKind::start})).code, CommandCode::applied);
}
TEST(SessionDiagnostics, UnknownExceptionsAreRedactedAndMessagesBounded) {
    const auto unknown = runtime::detail::exception_diagnostic(std::make_exception_ptr(std::runtime_error("private/path 0x1234")), {});
    EXPECT_EQ(unknown->reason, DiagnosticReason::unknown_exception);
    EXPECT_EQ(unknown->message.find("private"), std::string::npos);
    EXPECT_EQ(runtime::detail::exception_diagnostic(std::make_exception_ptr(std::future_error(std::future_errc::broken_promise)), {})->reason, DiagnosticReason::unknown_exception);
    EXPECT_EQ(runtime::detail::exception_diagnostic(std::make_exception_ptr(std::length_error("internal vector size")), {})->reason, DiagnosticReason::unknown_exception);
    const auto nonstandard = runtime::detail::exception_diagnostic(std::make_exception_ptr(42), {});
    EXPECT_EQ(nonstandard->message, unknown->message);
    const auto long_message = runtime::detail::exception_diagnostic(std::make_exception_ptr(std::logic_error(std::string(1000, 'x'))), {});
    EXPECT_EQ(long_message->message.size(), runtime::detail::diagnostic_message_limit);
    const auto sanitized = runtime::detail::exception_diagnostic(std::make_exception_ptr(std::logic_error("Invalid\nstate\tvalue")), {});
    EXPECT_EQ(sanitized->message, "Invalid state value");
    EXPECT_EQ(runtime::detail::exception_diagnostic(std::make_exception_ptr(std::bad_alloc()), {})->reason, DiagnosticReason::allocation_failure);
}
TEST(SessionDiagnostics, ConcurrentDiagnosticReadersDoNotChangeReplayOrSnapshots) {
    auto spec = core::conveyor_scenario(); spec.ticks = 512;
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 512;
    InteractiveSession session(1, spec, limits);
    finish(session.submit({1, 1, CommandKind::start}));
    auto rejected = finish(session.submit({99, 2, CommandKind::step}));
    auto held = rejected.diagnostic;
    std::atomic<bool> consistent{true};
    auto read = [&] {
        for (int i = 0; i < 10000; ++i) {
            const auto d = session.diagnostic();
            if (d != held || d->context.command_id != 2 || d->reason != DiagnosticReason::wrong_session) { consistent.store(false); }
        }
    };
    std::jthread a(read), b(read);
    runtime::SessionCommand run{1, 2, CommandKind::advance}; run.ticks = spec.ticks;
    EXPECT_EQ(finish(session.submit(run)).code, CommandCode::applied);
    a.join(); b.join(); EXPECT_TRUE(consistent.load());
    const auto history = session.history();
    EXPECT_FALSE(core::compare_runs(history->result, runtime::replay_session(*history->replay)));
    EXPECT_EQ(history->result.trace, core::simulate(spec).trace);
    EXPECT_EQ(held->reason, DiagnosticReason::wrong_session);
}
