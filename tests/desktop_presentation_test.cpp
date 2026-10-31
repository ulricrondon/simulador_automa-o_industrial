#include "../src/desktop/presentation/session_controller.hpp"
#include "sim/core/conveyor.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <new>
#include <system_error>
#include <thread>
using namespace sim;
using namespace sim::desktop;
using namespace std::chrono_literals;
namespace {
using runtime::CommandCode;
using runtime::CommandKind;
using runtime::DiagnosticReason;
using runtime::InteractiveState;
std::filesystem::path example(const char* name) { return std::filesystem::path(SIM_EXAMPLES_DIR) / name; }
// Polls as a GUI timer would, keeping everything each poll delivered.
struct Driver {
    SessionController controller;
    std::vector<FeedEntry> entries, alerts;
    explicit Driver(runtime::SessionLimits limits = {}) : controller(limits) {}
    void poll() {
        auto result = controller.poll();
        entries.insert(entries.end(), result.new_entries.begin(), result.new_entries.end());
        alerts.insert(alerts.end(), result.alerts.begin(), result.alerts.end());
    }
    void until(const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (true) {
            poll();
            if (condition()) { return; }
            if (std::chrono::steady_clock::now() > deadline) { throw std::runtime_error("Condition not reached"); }
            std::this_thread::sleep_for(1ms);
        }
    }
    void open(const char* name) { controller.open(example(name)); until([&] { return controller.idle() && controller.project(); }); }
    InteractiveState state() const { return controller.publication()->state; }
    core::Tick tick() const { return controller.publication()->next_tick; }
    void until_state(InteractiveState wanted) { until([&] { return controller.publication() && state() == wanted; }); }
    void until_tick(core::Tick wanted) { until([&] { return controller.publication() && tick() == wanted && controller.controls().step; }); }
    void start() { controller.start(); until_state(InteractiveState::running); until([&] { return controller.controls().step; }); }
};
std::string row_value(const std::vector<Row>& rows, const std::string& name) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.name == name; });
    return it == rows.end() ? "<absent>" : it->value;
}
std::size_t count_reason(const std::vector<FeedEntry>& entries, DiagnosticReason reason) {
    return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [&](const FeedEntry& entry) { return entry.diagnostic && entry.diagnostic->reason == reason; }));
}
}
TEST(DesktopControls, EnabledOperationsFollowTheRealState) {
    using S = InteractiveState;
    ControlInput in;
    EXPECT_EQ(compute_controls(in), (ControlState{.open = true}));
    in.project_loaded = true;
    EXPECT_FALSE(compute_controls(in).start); // Validated but not runnable: no conveyor.
    in.interactive_supported = true;
    EXPECT_EQ(compute_controls(in), (ControlState{.open = true, .start = true}));
    in.loading = true;
    EXPECT_EQ(compute_controls(in), ControlState{});
    in.loading = false;
    in.session_active = true; in.horizon = 10;
    in.state = S::running;
    EXPECT_EQ(compute_controls(in), (ControlState{.step = true, .advance = true, .pause = true, .stop = true, .close = true}));
    in.advance_in_flight = true;
    EXPECT_EQ(compute_controls(in), (ControlState{.pause = true, .stop = true, .close = true}));
    in.state = S::paused;
    EXPECT_EQ(compute_controls(in), (ControlState{.resume = true, .stop = true, .close = true}));
    in.advance_in_flight = false; in.stop_in_flight = true;
    EXPECT_EQ(compute_controls(in), (ControlState{.close = true}));
    in.stop_in_flight = false;
    for (const auto terminal : {S::completed, S::stopped}) {
        in.state = terminal;
        EXPECT_EQ(compute_controls(in), (ControlState{.close = true}));
    }
    in.state = S::failed; // The real reset still depends on stop.
    EXPECT_EQ(compute_controls(in), (ControlState{.stop = true, .close = true}));
    in.state = S::running; in.next_tick = 10;
    EXPECT_FALSE(compute_controls(in).step);
    in.next_tick = 0; in.closing = true;
    EXPECT_EQ(compute_controls(in), ControlState{});
    in.closing = false; in.shutting_down = true;
    EXPECT_EQ(compute_controls(in), (ControlState{.close = true}));
}
TEST(DesktopDiagnostics, SeverityComesFromTypedValuesNotFromTheText) {
    runtime::SessionDiagnostic d;
    d.message = "fatal failure execution_error";
    d.reason = DiagnosticReason::queue_capacity;
    EXPECT_EQ(classify(d, false), Severity::recoverable);
    d.message = "ok";
    for (const auto reason : {DiagnosticReason::snapshot_budget, DiagnosticReason::event_budget, DiagnosticReason::known_exception,
                              DiagnosticReason::unknown_exception, DiagnosticReason::allocation_failure}) {
        d.reason = reason;
        EXPECT_EQ(classify(d, false), Severity::fatal);
    }
    d.reason = DiagnosticReason::cancelled;
    EXPECT_EQ(classify(d, false), Severity::secondary);
    EXPECT_EQ(classify(d, true), Severity::fatal); // Being the published cause wins.
    for (const auto reason : {DiagnosticReason::invalid_state, DiagnosticReason::control_budget, DiagnosticReason::horizon, DiagnosticReason::stale_id}) {
        d.reason = reason;
        EXPECT_EQ(classify(d, false), Severity::recoverable);
    }
}
TEST(DesktopDiagnostics, ContextShowsOnlyPresentFields) {
    runtime::DiagnosticContext context;
    EXPECT_EQ(context_text(context), "etapa=admissão");
    context.command = CommandKind::step; context.command_id = 3; context.limit = 32;
    const auto text = context_text(context);
    EXPECT_NE(text.find("id=3"), std::string::npos);
    EXPECT_NE(text.find("limite=32"), std::string::npos);
    EXPECT_EQ(text.find("tick="), std::string::npos); // Not observed at admission.
    EXPECT_EQ(text.find("usado="), std::string::npos);
    context.tick = 0;
    EXPECT_NE(context_text(context).find("tick=0"), std::string::npos);
}
TEST(DesktopPanels, AbsentImageGivesNeutralValuesAndNoEstimates) {
    const auto spec = core::conveyor_scenario();
    const auto panel = make_signal_panel(nullptr, &spec);
    ASSERT_EQ(panel.inputs.size(), spec.program.input_count);
    for (const auto& row : panel.inputs) { EXPECT_EQ(row.value, std::string(neutral_value)); }
    for (const auto& row : panel.plant) { EXPECT_EQ(row.value, std::string(neutral_value)); }
    EXPECT_TRUE(make_signal_panel(nullptr, nullptr).inputs.empty());
    const auto state = make_state_panel(nullptr, 8, std::nullopt, &spec);
    EXPECT_EQ(state.state, std::string(neutral_value));
    EXPECT_EQ(state.position, std::string(neutral_value));
    EXPECT_EQ(state.tick, "0 / 8");
}
TEST(DesktopController, OpensARealProjectWithoutBlockingAndReportsInvalidOnes) {
    Driver d;
    d.controller.open(example("reference-conveyor.json"));
    EXPECT_FALSE(d.controller.idle());
    d.until([&] { return d.controller.project().has_value(); });
    const auto& project = *d.controller.project();
    EXPECT_EQ(project.title, "Reference - one physical passage");
    EXPECT_EQ(project.horizon, core::Tick{12});
    EXPECT_TRUE(project.interactive_supported);
    EXPECT_TRUE(d.controller.controls().start);
    EXPECT_TRUE(d.alerts.empty());
    EXPECT_EQ(row_value(d.controller.signal_panel().inputs, "I0"), std::string(neutral_value));

    d.controller.open(example("does-not-exist.json"));
    d.until([&] { return d.controller.idle() && !d.alerts.empty(); });
    ASSERT_EQ(d.alerts.size(), 1u);
    EXPECT_EQ(d.alerts[0].title, std::string(project_error_label(project::ErrorCode::io)));
    EXPECT_EQ(d.controller.project()->title, "Reference - one physical passage"); // Previous project stays.
}
TEST(DesktopController, MalformedJsonIsATypedOpenFailure) {
    const auto path = std::filesystem::temp_directory_path() / ("sim-desktop-" + project::generate_uuid_v4() + ".json");
    { std::ofstream(path) << "{ not json"; }
    Driver d;
    d.controller.open(path);
    d.until([&] { return d.controller.idle(); });
    std::filesystem::remove(path);
    ASSERT_EQ(d.alerts.size(), 1u);
    EXPECT_EQ(d.alerts[0].title, std::string(project_error_label(project::ErrorCode::invalid_json)));
    EXPECT_FALSE(d.controller.project());
    EXPECT_FALSE(d.controller.controls().start);
}
TEST(DesktopController, ProjectWithoutConveyorIsValidatedButNotStarted) {
    Driver d;
    d.open("reference-pulses.json");
    EXPECT_FALSE(d.controller.project()->interactive_supported);
    EXPECT_FALSE(d.controller.controls().start);
    d.controller.start();
    d.poll();
    EXPECT_FALSE(d.controller.session_active());
    ASSERT_EQ(d.alerts.size(), 1u);
}
TEST(DesktopController, FullOperationCycleMatchesTheBatchRun) {
    Driver d;
    d.open("reference-conveyor.json");
    const auto spec = project::load(example("reference-conveyor.json")).simulation;
    const auto expected = core::simulate(spec);
    d.start();
    EXPECT_EQ(d.controller.state_panel().state, std::string(state_label(InteractiveState::running)));
    EXPECT_EQ(d.controller.state_panel().image_tick, std::string(neutral_value));
    d.controller.step();
    d.until_tick(1);
    EXPECT_EQ(d.controller.state_panel().tick, "1 / 12");
    d.controller.advance(4);
    d.until_tick(5);
    const auto& image = *d.controller.publication()->latest;
    EXPECT_EQ(image, expected.trace[4]);
    const auto& panel = d.controller.signal_panel();
    EXPECT_EQ(row_value(panel.plant, "Motor"), std::string(core::name(expected.trace[4].conveyor->motor_state)));
    EXPECT_EQ(row_value(panel.plant, "Sensor publicado"), std::string(core::name(expected.trace[4].conveyor->sensor_state)));
    EXPECT_EQ(row_value(panel.outputs, "O0"), std::string(core::name(expected.trace[4].conveyor->plc_outputs[0].state)));
    EXPECT_NE(row_value(panel.blocks, "TON O0").find("ET=" + std::to_string(expected.trace[4].timers[0].elapsed_us)), std::string::npos);
    EXPECT_NE(row_value(panel.blocks, "CTU O1").find("CV=" + std::to_string(expected.trace[4].counters[1].cv)), std::string::npos);
    EXPECT_NE(row_value(panel.blocks, "Comparador O0 ramo 0"), "<absent>");
    EXPECT_EQ(d.controller.state_panel().position, std::to_string(expected.trace[4].position_um) + " µm (ocupa até " +
              std::to_string(expected.trace[4].position_um + spec.conveyor->piece_length_um) + " µm)");

    d.controller.pause();
    d.until_state(InteractiveState::paused);
    EXPECT_TRUE(d.controller.controls().resume);
    EXPECT_FALSE(d.controller.controls().step);
    d.controller.resume();
    d.until_state(InteractiveState::running);
    d.until([&] { return d.controller.controls().step; });
    d.controller.advance(7);
    d.until_state(InteractiveState::completed);
    d.until([&] { return !d.controller.controls().stop && d.controller.controls().close; });
    EXPECT_EQ(*d.controller.publication()->latest, expected.trace.back());
    EXPECT_TRUE(d.entries.empty());

    const auto events = d.controller.events();
    const auto total = expected.events.size() + expected.counter_events.size() + 2; // pause and resume
    EXPECT_EQ(events.size(), std::min(total, SessionController::max_event_rows));
    for (std::size_t i = 1; i < events.size(); ++i) { EXPECT_LE(events[i - 1].tick, events[i].tick); }

    d.controller.close_session();
    d.until([&] { return d.controller.idle(); });
    EXPECT_EQ(d.state(), InteractiveState::closed);
    EXPECT_TRUE(d.controller.controls().start);
    EXPECT_TRUE(d.controller.controls().open);
    EXPECT_EQ(*d.controller.publication()->latest, expected.trace.back()); // Still readable after teardown.
}
TEST(DesktopController, RestartUsesANewSessionAndReproducesTheResult) {
    Driver d;
    d.open("reference-conveyor.json");
    std::vector<core::Snapshot> images;
    std::vector<std::uint64_t> sessions;
    for (int run = 0; run < 2; ++run) {
        d.start();
        d.controller.advance(12);
        d.until_state(InteractiveState::completed);
        images.push_back(*d.controller.publication()->latest);
        sessions.push_back(d.controller.publication()->session_id);
        d.controller.close_session();
        d.until([&] { return d.controller.idle(); });
    }
    EXPECT_EQ(images[0], images[1]);
    EXPECT_NE(sessions[0], sessions[1]);
    EXPECT_TRUE(d.entries.empty()); // Restarted command IDs are not stale in the new session.
}
TEST(DesktopController, StopShowsTheMotorAfterControlsWhileTheImageStaysTheLastScan) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(5);
    d.until_tick(5);
    const auto spec = project::load(example("reference-conveyor.json")).simulation;
    ASSERT_EQ(core::simulate(spec).trace[4].conveyor->motor_state, core::DigitalState::HIGH);
    EXPECT_EQ(d.controller.state_panel().control_motor, "HIGH");
    d.controller.stop();
    d.until_state(InteractiveState::stopped);
    EXPECT_EQ(d.controller.state_panel().control_motor, "LOW");
    EXPECT_EQ(row_value(d.controller.signal_panel().plant, "Motor"), "HIGH");
    EXPECT_EQ(d.controller.state_panel().image_tick, "4");
    EXPECT_EQ(d.tick(), core::Tick{5});
}
TEST(DesktopController, StopIsAppliedWhileOrdinaryCommandsAreRejected) {
    runtime::SessionLimits limits; limits.queue_capacity = 1; limits.commands = 1;
    const auto path = std::filesystem::temp_directory_path() / ("sim-desktop-" + project::generate_uuid_v4() + ".json");
    auto document = project::new_conveyor_demo("Fila");
    document.simulation.ticks = 100000;
    { std::ofstream(path, std::ios::binary) << project::serialize(document); }
    Driver d(limits);
    d.controller.open(path);
    d.until([&] { return d.controller.idle() && d.controller.project(); });
    std::filesystem::remove(path);
    d.start();
    d.controller.advance(100000);
    d.controller.pause(); // Beyond the control budget of one admission.
    d.poll();
    EXPECT_EQ(count_reason(d.entries, DiagnosticReason::control_budget), 1u);
    d.controller.stop();
    d.until_state(InteractiveState::stopped);
    EXPECT_FALSE(d.controller.controls().stop);
    EXPECT_EQ(d.controller.state_panel().control_motor, "LOW");
    EXPECT_FALSE(d.controller.failure()); // A rejection did not interrupt the simulation.
}
TEST(DesktopController, RejectionIsReportedOnceAndDoesNotEndTheSession) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(13); // Past the 12-tick horizon.
    for (int i = 0; i < 5; ++i) { d.poll(); std::this_thread::sleep_for(1ms); }
    d.until([&] { return !d.entries.empty(); });
    for (int i = 0; i < 20; ++i) { d.poll(); std::this_thread::sleep_for(1ms); }
    EXPECT_EQ(count_reason(d.entries, DiagnosticReason::horizon), 1u);
    EXPECT_EQ(d.entries[0].severity, Severity::recoverable);
    EXPECT_NE(d.entries[0].context.find("limite=12"), std::string::npos);
    EXPECT_TRUE(d.alerts.empty());
    EXPECT_EQ(d.state(), InteractiveState::running);
    EXPECT_FALSE(d.controller.failure());
    EXPECT_TRUE(d.controller.controls().step);
}
TEST(DesktopController, FatalCauseAlertsOnceAndSurvivesTheSessionEnd) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 2;
    Driver d(limits);
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(5);
    d.until_state(InteractiveState::failed);
    d.until([&] { return d.controller.failure().has_value(); });
    for (int i = 0; i < 20; ++i) { d.poll(); }
    ASSERT_EQ(d.alerts.size(), 1u);
    EXPECT_EQ(d.alerts[0].diagnostic->reason, DiagnosticReason::snapshot_budget);
    EXPECT_EQ(d.alerts[0].severity, Severity::fatal);
    EXPECT_EQ(count_reason(d.entries, DiagnosticReason::snapshot_budget), 1u);
    EXPECT_TRUE(d.controller.controls().stop);
    EXPECT_FALSE(d.controller.controls().step);
    d.controller.close_session();
    d.until([&] { return d.controller.idle(); });
    EXPECT_EQ(d.state(), InteractiveState::closed);
    ASSERT_TRUE(d.controller.failure());
    EXPECT_EQ(d.controller.failure()->diagnostic->reason, DiagnosticReason::snapshot_budget);
    EXPECT_EQ(d.controller.publication()->error, CommandCode::history_limit);
    EXPECT_EQ(d.alerts.size(), 1u);
    d.controller.start();
    d.poll();
    EXPECT_FALSE(d.controller.failure()); // Cleared only by an explicit new session.
}
TEST(DesktopController, FatalCauseIsNotAlertedAgainAfterLeavingTheBoundedFeed) {
    runtime::SessionLimits limits; limits.history = runtime::HistoryMode::full; limits.snapshots = 1;
    Driver d(limits);
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(5);
    d.until([&] { return d.controller.failure().has_value(); });
    for (std::size_t i = 0; i <= SessionController::max_feed_entries; ++i) {
        d.controller.advance(100000); // Rejected at admission: each one is a new diagnostic.
        d.poll();
    }
    EXPECT_EQ(d.controller.feed().size(), SessionController::max_feed_entries);
    EXPECT_EQ(d.alerts.size(), 1u);
    EXPECT_EQ(count_reason(d.entries, DiagnosticReason::snapshot_budget), 1u);
    EXPECT_EQ(d.controller.failure()->diagnostic->reason, DiagnosticReason::snapshot_budget);
}
TEST(DesktopController, CloseDuringAdvanceAndDuringPauseDoesNotWaitOnTheCaller) {
    for (const bool paused : {false, true}) {
        auto document = project::new_conveyor_demo("Encerramento");
        document.simulation.ticks = 100000;
        const auto path = std::filesystem::temp_directory_path() / ("sim-desktop-" + project::generate_uuid_v4() + ".json");
        { std::ofstream(path, std::ios::binary) << project::serialize(document); }
        Driver d;
        d.controller.open(path);
        d.until([&] { return d.controller.idle() && d.controller.project(); });
        std::filesystem::remove(path);
        d.start();
        d.controller.advance(100000);
        if (paused) {
            d.controller.pause();
            d.until_state(InteractiveState::paused);
        }
        d.controller.close_session();
        d.poll();
        EXPECT_FALSE(d.controller.controls().close); // Not offered twice while closing.
        EXPECT_FALSE(d.controller.controls().stop);
        d.until([&] { return d.controller.idle(); });
        EXPECT_EQ(d.state(), InteractiveState::closed);
        EXPECT_FALSE(d.controller.session_active());
        EXPECT_FALSE(d.controller.failure()); // Closing is not a failure.
        EXPECT_TRUE(d.alerts.empty());
        for (const auto& entry : d.entries) { EXPECT_EQ(entry.severity, Severity::secondary); }
    }
}
TEST(DesktopController, ShutdownReachesIdleAndBlocksNewWork) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.request_shutdown();
    d.controller.start();
    d.controller.step();
    d.until([&] { return d.controller.idle(); });
    EXPECT_FALSE(d.controller.session_active());
    EXPECT_EQ(d.controller.controls(), ControlState{});
    d.controller.start();
    d.controller.open(example("conveyor.json"));
    EXPECT_FALSE(d.controller.session_active());
    EXPECT_TRUE(d.controller.idle());
}
TEST(DesktopController, DestroyingAControllerWithAnActiveSessionIsSafe) {
    auto driver = std::make_unique<Driver>();
    driver->open("reference-conveyor.json");
    driver->start();
    driver->controller.advance(12);
    driver.reset();
    auto loading = std::make_unique<Driver>();
    loading->controller.open(example("reference-conveyor.json"));
    loading.reset();
    SUCCEED();
}
TEST(DesktopController, UnchangedPublicationDoesNotRequestWidgetUpdates) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.step();
    d.until_tick(1);
    d.poll();
    for (int i = 0; i < 10; ++i) {
        const auto result = d.controller.poll();
        EXPECT_FALSE(result.state_changed);
        EXPECT_FALSE(result.signals_changed);
        EXPECT_FALSE(result.events_changed);
        EXPECT_FALSE(result.controls_changed);
        EXPECT_FALSE(result.project_changed);
        EXPECT_TRUE(result.new_entries.empty());
    }
}
TEST(DesktopController, OnlyTheLatestImageStaysPinnedAndRowsAreBounded) {
    auto document = project::new_conveyor_demo("Retencao");
    document.simulation.ticks = 3000;
    document.simulation.conveyor->events.clear();
    for (core::Tick tick = 0; tick < 3000; ++tick) {
        document.simulation.conveyor->events.push_back({tick, tick, core::ExternalTarget::command, tick % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    const auto path = std::filesystem::temp_directory_path() / ("sim-desktop-" + project::generate_uuid_v4() + ".json");
    { std::ofstream(path, std::ios::binary) << project::serialize(document); }
    runtime::SessionLimits limits; limits.snapshots = 1;
    Driver d(limits);
    d.controller.open(path);
    d.until([&] { return d.controller.idle() && d.controller.project(); });
    std::filesystem::remove(path);
    d.start();
    d.controller.step();
    d.until_tick(1);
    const std::weak_ptr<const core::Snapshot> first = d.controller.publication()->latest;
    d.controller.advance(2999);
    d.until_state(InteractiveState::completed);
    EXPECT_TRUE(first.expired());
    // At most, not exactly: the observation window may evict rows between polls at this speed.
    // EventsLostFromTheWindow... covers reaching the exact bound deterministically.
    EXPECT_LE(d.controller.events().size(), SessionController::max_event_rows);
    EXPECT_NE(d.controller.events().back().text.find("tick 2999"), std::string::npos);
}
namespace {
// A conveyor project whose command toggles on every tick, so each scan produces events.
project::Document busy_project(core::Tick ticks) {
    auto document = project::new_conveyor_demo("Carga");
    document.simulation.ticks = ticks;
    document.simulation.conveyor->events.clear();
    for (core::Tick tick = 0; tick < ticks; ++tick) {
        document.simulation.conveyor->events.push_back({tick, tick, core::ExternalTarget::command, tick % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    return document;
}
struct TempProject {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("sim-desktop-" + project::generate_uuid_v4() + ".json");
    explicit TempProject(const project::Document& document) { std::ofstream(path, std::ios::binary) << project::serialize(document); }
    ~TempProject() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};
void open_file(Driver& d, const std::filesystem::path& path) {
    d.controller.open(path);
    d.until([&] { return d.controller.idle() && d.controller.project(); });
}
runtime::ObservationEvent make_event(std::uint64_t sequence, std::uint64_t boundary, std::uint64_t time_us, core::Tick tick, core::Phase phase, std::uint32_t order) {
    return {sequence, boundary, tick, time_us, phase, order, core::Transition{tick, time_us, phase}};
}
}
TEST(DesktopController, PollingDuringAdvanceAlwaysShowsOneBoundary) {
    constexpr core::Tick ticks = 20000;
    const auto document = busy_project(ticks);
    const auto expected = core::simulate(document.simulation);
    TempProject file(document);
    runtime::SessionLimits limits; limits.snapshots = 1; limits.events = 1000000;
    Driver d(limits);
    open_file(d, file.path);
    d.start();
    d.controller.advance(ticks);
    std::size_t images = 0;
    while (true) {
        d.poll();
        const auto& observation = *d.controller.observation();
        const auto& publication = observation.publication;
        ASSERT_TRUE(observation.control);
        ASSERT_EQ(observation.control->next_tick, publication.next_tick);
        const auto& panel = d.controller.state_panel();
        ASSERT_EQ(panel.sequence, publication.sequence);
        ASSERT_EQ(panel.tick, std::to_string(publication.next_tick) + " / " + std::to_string(ticks));
        if (publication.latest) {
            ++images;
            const auto& image = *publication.latest;
            ASSERT_EQ(image.tick + 1, publication.next_tick);
            ASSERT_EQ(image, expected.trace[image.tick]); // Same data as the batch run for that tick.
            ASSERT_EQ(panel.image_tick, std::to_string(image.tick));
            ASSERT_EQ(row_value(d.controller.signal_panel().plant, "Motor"), std::string(core::name(image.conveyor->motor_state)));
            ASSERT_EQ(panel.control_motor, std::string(core::name(observation.control->motor))); // Never from another boundary.
        }
        if (publication.state == InteractiveState::completed) { break; }
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_GT(images, 1u);
    EXPECT_TRUE(d.entries.empty());
}
TEST(DesktopController, RestartBeforeTheFirstTickShowsNothingFromTheOldSession) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(6);
    d.until_tick(6);
    ASSERT_NE(row_value(d.controller.signal_panel().plant, "Motor"), std::string(neutral_value));
    ASSERT_FALSE(d.controller.events().empty());
    d.controller.close_session();
    d.until([&] { return d.controller.idle(); });
    EXPECT_EQ(d.controller.state_panel().image_tick, "5"); // A closed session stays readable.

    const std::string absent(neutral_value);
    const auto expect_absent = [&] {
        const auto& panel = d.controller.signal_panel();
        for (const auto* rows : {&panel.inputs, &panel.outputs, &panel.plant}) {
            ASSERT_FALSE(rows->empty());
            for (const auto& row : *rows) { EXPECT_EQ(row.value, absent) << row.name; }
        }
        EXPECT_NE(row_value(panel.blocks, "TON O0").find("ET=" + absent), std::string::npos);
        EXPECT_NE(row_value(panel.blocks, "CTU O1").find("CV=" + absent), std::string::npos);
        EXPECT_NE(row_value(panel.blocks, "Comparador O0 ramo 0").find("→ " + absent), std::string::npos);
        EXPECT_EQ(d.controller.state_panel().image_tick, absent);
        EXPECT_EQ(d.controller.state_panel().position, absent);
        EXPECT_TRUE(d.controller.events().empty());
    };
    d.controller.start();
    expect_absent(); // Already invalidated, before any poll.
    EXPECT_FALSE(d.controller.publication());
    auto result = d.controller.poll();
    EXPECT_TRUE(result.signals_changed);
    EXPECT_TRUE(result.state_changed);
    EXPECT_TRUE(result.events_reset);
    EXPECT_EQ(result.events_added, 0u);
    d.until_state(InteractiveState::running);
    d.until([&] { return d.controller.controls().step; });
    expect_absent(); // The first publication has a state, but no image yet.
    EXPECT_EQ(d.controller.state_panel().state, std::string(state_label(InteractiveState::running)));
    EXPECT_EQ(d.controller.state_panel().tick, "0 / 12");
    EXPECT_EQ(d.controller.state_panel().lost_events, "0");
    EXPECT_EQ(d.controller.publication()->session_id, 2u);
    d.controller.step();
    d.until_tick(1);
    EXPECT_EQ(d.controller.state_panel().image_tick, "0"); // The cursor restarted with the session.
    EXPECT_NE(row_value(d.controller.signal_panel().plant, "Motor"), absent);
}
TEST(DesktopController, OpeningAnotherProjectClearsTheClosedSession) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(6);
    d.until_tick(6);
    d.controller.close_session();
    d.until([&] { return d.controller.idle(); });
    d.controller.open(example("reference-conveyor.json"));
    d.until([&] { return d.controller.idle() && !d.controller.publication(); });
    EXPECT_EQ(row_value(d.controller.signal_panel().plant, "Motor"), std::string(neutral_value));
    EXPECT_TRUE(d.controller.events().empty());
    EXPECT_EQ(d.controller.state_panel().state, std::string(neutral_value));
}
TEST(DesktopEvents, OrderFollowsTheFullKeyNotTheArrivalOrTickAlone) {
    using P = core::Phase;
    std::vector<runtime::ObservationEvent> events{
        make_event(6, 2, 1000, 0, P::control, 0),  // Same time as the scan below, later boundary.
        make_event(4, 1, 2000, 1, P::external, 0), // Later time, earlier phase.
        make_event(3, 1, 1000, 0, P::sensor, 1),
        make_event(1, 1, 1000, 0, P::ladder, 0),   // Counter update precedes the coil.
        make_event(2, 1, 1000, 0, P::ladder, 1),
        make_event(0, 1, 1000, 0, P::external, 1),
        make_event(5, 1, 2000, 1, P::external, 0), // Everything equal but the sequence.
    };
    order_events(events);
    for (std::size_t i = 0; i < events.size(); ++i) { EXPECT_EQ(events[i].sequence, i); }
}
TEST(DesktopController, SameTickEventsOfDifferentPhasesAndTypesAreNotConcatenated) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(12);
    d.until_state(InteractiveState::completed);
    d.until([&] { return d.controller.controls().close && !d.controller.controls().stop; });
    const auto page = runtime::observation_events(*d.controller.observation(), {d.controller.publication()->session_id, 0}, 8192);
    const auto& rows = d.controller.events();
    ASSERT_EQ(rows.size(), page.events.size());
    for (std::size_t i = 0; i < rows.size(); ++i) { EXPECT_EQ(rows[i], event_row(page.events[i])); }
    // Tick 5: the sensor sample comes before the counter, and the coil after it. Concatenating
    // transitions then counters would put the counter last.
    const auto index_of = [&](const char* text, const char* other) {
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const EventRow& row) {
            return row.tick == 5 && row.text.find(text) != std::string::npos && row.text.find(other) != std::string::npos;
        });
        EXPECT_NE(it, rows.end()) << text << " " << other;
        return it - rows.begin();
    };
    const auto input = index_of("entrada PLC 1", "[amostragem]");
    const auto counter = index_of("[contador]", "CTU O1");
    const auto coil = index_of("saída PLC 1", "[ladder]");
    EXPECT_LT(input, counter);
    EXPECT_LT(counter, coil);
}
TEST(DesktopController, EventsLostFromTheWindowAreShownOnceAndTheCursorMovesOn) {
    auto document = busy_project(3);
    document.simulation.conveyor->events.clear();
    for (std::uint64_t i = 0; i < 9000; ++i) { // One tick publishes more than the window retains.
        document.simulation.conveyor->events.push_back({0, i, core::ExternalTarget::command, i % 2 ? core::DigitalState::LOW : core::DigitalState::HIGH});
    }
    document.simulation.conveyor->events.push_back({1, 9000, core::ExternalTarget::command, core::DigitalState::HIGH}); // Ends LOW on tick 0.
    TempProject file(document);
    Driver d;
    open_file(d, file.path);
    d.start();
    EXPECT_EQ(d.controller.state_panel().lost_events, "0");
    d.controller.step();
    d.until_tick(1);
    const auto& observation = *d.controller.observation();
    const auto first = observation.events->first_sequence;
    ASSERT_GT(first, 0u);
    EXPECT_EQ(d.controller.state_panel().lost_events, std::to_string(first));
    const auto& rows = d.controller.events();
    ASSERT_EQ(rows.size(), SessionController::max_event_rows);
    EXPECT_TRUE(rows.front().lost);
    EXPECT_NE(rows.front().text.find(std::to_string(first) + " evento(s) perdido(s)"), std::string::npos);
    EXPECT_EQ(std::count_if(rows.begin(), rows.end(), [](const EventRow& row) { return row.lost; }), 1);
    // Newest rows, in order, from the end of the same observation.
    const auto page = runtime::observation_events(observation, {observation.publication.session_id, first}, 8192);
    EXPECT_EQ(rows.back(), event_row(page.events.back()));

    // Already consumed: neither repeated nor counted again.
    for (int i = 0; i < 5; ++i) { EXPECT_FALSE(d.controller.poll().events_changed); }
    d.controller.step();
    d.until_tick(2);
    EXPECT_EQ(d.controller.state_panel().lost_events, std::to_string(first)); // Outlives the gap row.
    EXPECT_LE(std::count_if(rows.begin(), rows.end(), [](const EventRow& row) { return row.lost; }), 1);
    EXPECT_NE(rows.back().text.find("tick 1"), std::string::npos);
    EXPECT_TRUE(d.alerts.empty()); // A gap in the display is not a simulation failure.
    d.controller.close_session();
    d.until([&] { return d.controller.idle(); });
    d.start();
    EXPECT_EQ(d.controller.state_panel().lost_events, "0");
}
TEST(DesktopController, PollDeliversEachEventRowOnce) {
    const auto document = busy_project(3000);
    TempProject file(document);
    Driver d;
    open_file(d, file.path);
    d.start();
    d.controller.advance(3000);
    std::deque<std::string> shown;
    bool done = false;
    while (!done) {
        const auto result = d.controller.poll();
        if (result.events_reset) { shown.clear(); }
        const auto& rows = d.controller.events();
        ASSERT_LE(result.events_added, rows.size());
        for (auto i = rows.size() - result.events_added; i < rows.size(); ++i) { shown.push_back(rows[i].text); }
        while (shown.size() > SessionController::max_event_rows) { shown.pop_front(); }
        done = d.controller.publication() && d.state() == InteractiveState::completed && result.events_added == 0;
        std::this_thread::sleep_for(1ms);
    }
    // What a widget rebuilt from the increments shows is exactly the controller's list.
    ASSERT_EQ(shown.size(), d.controller.events().size());
    for (std::size_t i = 0; i < shown.size(); ++i) { EXPECT_EQ(shown[i], d.controller.events()[i].text); }
}
TEST(DesktopController, RefusedAsyncTaskBecomesADiagnosticAndNothingElseChanges) {
    int refusals = 0;
    bool refuse_with_system_error = true;
    const auto gate = [&] {
        if (refusals == 0) { return; }
        --refusals;
        if (refuse_with_system_error) { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); }
        throw std::bad_alloc();
    };
    SessionController controller({}, gate);
    const auto poll_until = [&](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        std::vector<FeedEntry> alerts;
        while (true) {
            auto result = controller.poll();
            alerts.insert(alerts.end(), result.alerts.begin(), result.alerts.end());
            if (condition()) { return alerts; }
            if (std::chrono::steady_clock::now() > deadline) { throw std::runtime_error("Condition not reached"); }
            std::this_thread::sleep_for(1ms);
        }
    };
    refusals = 1;
    EXPECT_NO_THROW(controller.open(example("reference-conveyor.json")));
    EXPECT_TRUE(controller.idle());
    auto alerts = poll_until([] { return true; });
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, Severity::fatal);
    EXPECT_FALSE(controller.project());
    EXPECT_TRUE(controller.controls().open); // Can try again.

    controller.open(example("reference-conveyor.json"));
    poll_until([&] { return controller.project().has_value(); });

    refusals = 1; refuse_with_system_error = false;
    EXPECT_NO_THROW(controller.start());
    EXPECT_TRUE(controller.idle());
    EXPECT_FALSE(controller.session_active());
    alerts = poll_until([] { return true; });
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].title, "Não foi possível criar a sessão");
    EXPECT_TRUE(controller.controls().start);

    controller.start();
    poll_until([&] { return controller.publication() && controller.publication()->state == InteractiveState::running; });
    EXPECT_EQ(controller.publication()->session_id, 1u); // A refused start used no ID.
    controller.request_shutdown();
    poll_until([&] { return controller.idle(); });
}
TEST(DesktopController, FailureInsideTheStartTaskIsReported) {
    runtime::SessionLimits invalid; invalid.queue_capacity = 0;
    Driver d(invalid);
    d.open("reference-conveyor.json");
    d.controller.start();
    EXPECT_FALSE(d.controller.idle());
    d.until([&] { return d.controller.idle(); });
    ASSERT_EQ(d.alerts.size(), 1u);
    EXPECT_EQ(d.alerts[0].title, "Não foi possível criar a sessão");
    EXPECT_FALSE(d.controller.session_active());
    EXPECT_TRUE(d.controller.controls().start);
}
TEST(DesktopController, ShutdownDuringLoadingDiscardsTheResultQuietly) {
    for (const auto* name : {"reference-conveyor.json", "does-not-exist.json"}) {
        Driver d;
        d.controller.open(example(name));
        d.controller.request_shutdown();
        EXPECT_FALSE(d.controller.idle()); // The task is still owned: the window must keep waiting.
        d.until([&] { return d.controller.idle(); });
        EXPECT_FALSE(d.controller.project());
        EXPECT_TRUE(d.alerts.empty());
        EXPECT_TRUE(d.entries.empty());
        EXPECT_EQ(d.controller.controls(), ControlState{});
    }
}
TEST(DesktopController, ShutdownWhileTheSessionIsBeingCreatedClosesIt) {
    Driver d;
    d.open("reference-conveyor.json");
    d.controller.start();
    d.controller.request_shutdown();
    EXPECT_FALSE(d.controller.idle());
    d.until([&] { return d.controller.idle(); });
    EXPECT_FALSE(d.controller.session_active());
    EXPECT_TRUE(d.alerts.empty());
    if (d.controller.publication()) { EXPECT_EQ(d.state(), InteractiveState::closed); }
}
TEST(DesktopController, ComparatorShowsWhatTheNextScanReadsAfterCvChanges) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    d.controller.advance(5);
    d.until_tick(5);
    const auto comparator = [&] { return row_value(d.controller.signal_panel().blocks, "Comparador O0 ramo 0"); };
    const auto counter = [&] { return row_value(d.controller.signal_panel().blocks, "CTU O1"); };
    const auto motor = [&] { return row_value(d.controller.signal_panel().outputs, "O0"); };
    EXPECT_EQ(d.controller.state_panel().image_tick, "4");
    EXPECT_NE(counter().find("CV=0"), std::string::npos);
    EXPECT_NE(comparator().find("→ HIGH (próxima varredura)"), std::string::npos);
    EXPECT_EQ(motor(), "HIGH");

    d.controller.step();
    d.until_tick(6);
    EXPECT_NE(counter().find("CV=1"), std::string::npos);
    EXPECT_NE(comparator().find("→ LOW (próxima varredura)"), std::string::npos); // Already the new CV...
    EXPECT_EQ(motor(), "HIGH");                                                   // ...this scan read the old one.

    d.controller.step();
    d.until_tick(7);
    EXPECT_NE(comparator().find("→ LOW (próxima varredura)"), std::string::npos);
    EXPECT_EQ(motor(), "LOW"); // The scan that read CV=1.
}
TEST(DesktopController, OnlyTheCurrentObservationIsRetained) {
    Driver d;
    d.open("reference-conveyor.json");
    d.start();
    const std::weak_ptr<const runtime::SessionObservation> first = d.controller.observation();
    const std::weak_ptr<const runtime::ObservationEvents> window = d.controller.observation()->events;
    d.controller.advance(12);
    d.until_state(InteractiveState::completed);
    EXPECT_TRUE(first.expired());
    EXPECT_TRUE(window.expired());
}
TEST(DesktopController, LargeProjectLeavesTheCallerResponsive) {
    const auto document = busy_project(20000);
    TempProject file(document);
    using clock = std::chrono::steady_clock;
    const auto timed = [](auto&& action) {
        const auto begin = clock::now();
        action();
        return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - begin).count();
    };
    runtime::SessionLimits limits; limits.events = 1000000;
    Driver d(limits);
    const auto open_us = timed([&] { d.controller.open(file.path); });
    long long worst_poll_us = 0;
    const auto poll_until = [&](auto condition) {
        while (true) {
            worst_poll_us = std::max<long long>(worst_poll_us, timed([&] { d.poll(); }));
            if (condition()) { return; }
            std::this_thread::sleep_for(1ms);
        }
    };
    poll_until([&] { return d.controller.idle() && d.controller.project(); });
    const auto start_us = timed([&] { d.controller.start(); });
    poll_until([&] { return d.controller.controls().step; });
    const auto advance_us = timed([&] { d.controller.advance(20000); });
    poll_until([&] { return d.state() == InteractiveState::completed; });
    const auto close_us = timed([&] { d.controller.close_session(); });
    poll_until([&] { return d.controller.idle(); });
    RecordProperty("open_us", static_cast<int>(open_us));
    RecordProperty("start_us", static_cast<int>(start_us));
    RecordProperty("worst_poll_us", static_cast<int>(worst_poll_us));
    std::cout << "open=" << open_us << "us start=" << start_us << "us advance=" << advance_us << "us close=" << close_us << "us worst_poll=" << worst_poll_us << "us\n";
    // Far above the expected cost: only a wait for the worker or for ticks would reach it.
    constexpr long long limit_us = 1000000;
    EXPECT_LT(open_us, limit_us);
    EXPECT_LT(start_us, limit_us);
    EXPECT_LT(advance_us, limit_us);
    EXPECT_LT(close_us, limit_us);
    EXPECT_LT(worst_poll_us, limit_us);
    EXPECT_TRUE(d.alerts.empty());
}
