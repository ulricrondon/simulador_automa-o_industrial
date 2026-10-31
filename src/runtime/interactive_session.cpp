#include "sim/runtime/interactive_session.hpp"
#include "sim/core/conveyor.hpp"
#include "session_diagnostics.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iterator>

namespace sim::runtime {
namespace {
SessionLimits checked(SessionLimits limits) {
    if (limits.queue_capacity == 0 || limits.queue_capacity > 1024 || limits.snapshots == 0 || limits.snapshots > 100000 ||
        limits.events == 0 || limits.events > 1000000 || limits.commands == 0 || limits.commands > 100000 ||
        limits.functional_commands == 0 || limits.functional_commands > 100000 ||
        limits.advance_requests == 0 || limits.advance_requests > 100000 ||
        (limits.history != HistoryMode::full && limits.history != HistoryMode::bounded)) {
        throw std::invalid_argument("Invalid interactive session limits");
    }
    return limits;
}
core::RunSpec legacy_configuration(const std::shared_ptr<Runner>& runner, core::RunSpec spec) {
    if (!runner) { throw std::invalid_argument("Null compatibility Runner"); }
    return spec;
}
core::SessionStatus status(const core::ConveyorSession& session) {
    return {session.state(), session.next_tick(), session.position_um(), session.motor_state()};
}
// Append-only log: full blocks become immutable and are shared, never copied.
template<class T> class SharedLog {
public:
    void push(T value) {
        tail_.push_back(std::move(value));
        ++size_;
        if (tail_.size() == block_size) {
            sealed_.push_back(std::make_shared<const std::vector<T>>(std::move(tail_)));
            tail_.clear();
        }
    }
    std::size_t size() const noexcept { return size_; }
    HistorySegments<T> share() const {
        HistorySegments<T> segments{sealed_, size_};
        if (!tail_.empty()) { segments.chunks.push_back(std::make_shared<const std::vector<T>>(tail_)); }
        return segments;
    }
private:
    static constexpr std::size_t block_size = 4096;
    std::vector<std::shared_ptr<const std::vector<T>>> sealed_;
    std::vector<T> tail_;
    std::size_t size_{};
};
}
struct InteractiveSession::Data {
    struct Channel { std::promise<CommandReply> promise; bool settled{}; };
    struct Request {
        SessionCommand command;
        std::shared_ptr<Channel> channel;
        core::Tick begin{}, advanced{};
    };
    const SessionLimits limits;
    const std::uint64_t session_id;
    const core::Tick horizon;
    const std::shared_ptr<const core::RunSpec> configuration;
    std::vector<core::Tick> configured_ticks; // Sorted tick of each configured event.
    std::size_t plant_records{}; // Most records one step can emit besides external inputs.
    std::mutex admission;
    std::condition_variable ready;
    std::deque<Request> queue;
    std::optional<Request> stop_slot;
    bool closing{}, stop_requested{};
    std::uint64_t last_id{};
    std::size_t controls{}, inputs{}, advances{};
    // Guards retained records and is held for a step, never for a consumer's deep copy.
    mutable std::mutex mutex;
    SessionPublication current;
    std::deque<std::shared_ptr<const core::Snapshot>> trace;
    SharedLog<core::Transition> events;
    SharedLog<core::CounterEvent> counter_events;
    std::optional<core::SessionStatus> control;
    SharedLog<CommandRecord> commands;
    SharedLog<ReplayAction> journal;
    // Separate from mutex: publication() waits only for a pointer swap.
    mutable std::mutex publication_mutex;
    std::shared_ptr<const SessionPublication> published;
    detail::DiagnosticStore diagnostics;
    std::uint64_t applied{};
    std::promise<void> finished;
    std::shared_future<void> completion = finished.get_future().share();

    Data(std::uint64_t id, core::RunSpec spec, SessionLimits settings)
        : limits(settings), session_id(id), horizon(spec.ticks),
          configuration(std::make_shared<const core::RunSpec>(std::move(spec))) {
        const auto& program = configuration->program;
        const auto& conveyor = *configuration->conveyor;
        for (const auto& event : conveyor.events) { configured_ticks.push_back(event.tick); }
        std::sort(configured_ticks.begin(), configured_ticks.end());
        // Unwired inputs and coils without a rung never change; motor, piece and sensor emit one each.
        const auto wired_inputs = std::count_if(conveyor.connections.begin(), conveyor.connections.end(),
            [](const auto& wire) { return wire.target.port == core::Port::plc_input; });
        const auto counters = std::count_if(program.rungs.begin(), program.rungs.end(),
            [](const auto& rung) { return rung.ctu.has_value(); });
        plant_records = static_cast<std::size_t>(wired_inputs) + program.rungs.size() + static_cast<std::size_t>(counters) + 3;
        current.session_id = id;
        published = std::make_shared<const SessionPublication>(current);
    }
    std::size_t retained_events() const noexcept { return events.size() + counter_events.size(); }
    std::size_t step_records(core::Tick boundary, std::size_t boundary_inputs) const {
        const auto range = std::equal_range(configured_ticks.begin(), configured_ticks.end(), boundary);
        return static_cast<std::size_t>(std::distance(range.first, range.second)) + boundary_inputs + plant_records;
    }
    void publish() {
        auto& p = current;
        ++p.sequence;
        p.first_tick.reset(); p.last_tick.reset();
        if (!trace.empty()) { p.first_tick = trace.front()->tick; p.last_tick = trace.back()->tick; }
        auto next = std::make_shared<const SessionPublication>(p);
        std::lock_guard lock(publication_mutex);
        published.swap(next);
    }
    DiagnosticContext context(const SessionCommand& command, DiagnosticStage stage,
                              std::optional<core::Tick> tick = {}) const {
        DiagnosticContext value;
        value.session_id = session_id; value.command_id = command.id; value.command = command.kind;
        value.stage = stage; value.tick = tick;
        if (stage != DiagnosticStage::admission) { value.state = current.state; }
        return value;
    }
    DiagnosticPtr application_diagnostic(const Request& request, CommandCode code, DiagnosticReason reason,
        std::optional<std::uint64_t> used = {}, std::optional<std::uint64_t> limit = {},
        std::optional<std::uint64_t> requested = {}) const {
        auto value = context(request.command, DiagnosticStage::application, current.next_tick);
        value.used = used; value.limit = limit; value.requested = requested;
        return detail::make_diagnostic(code, reason, std::move(value));
    }
    void settle(const Request& r, CommandCode code, core::Tick tick, DiagnosticPtr diagnostic = {}) {
        if (r.channel->settled) { return; }
        if (code != CommandCode::applied && !diagnostic) {
            const auto reason = code == CommandCode::cancelled ? DiagnosticReason::cancelled :
                code == CommandCode::stale ? DiagnosticReason::stale_boundary :
                code == CommandCode::execution_error ? DiagnosticReason::unknown_exception : DiagnosticReason::invalid_state;
            diagnostic = application_diagnostic(r, code, reason);
        }
        CommandReply reply{r.command.id, code, tick, code == CommandCode::applied ? ++applied : 0, r.advanced, diagnostic};
        commands.push({r.command, reply});
        if (diagnostic && !(current.diagnostic && diagnostic != current.diagnostic &&
            (code == CommandCode::cancelled || code == CommandCode::execution_error))) { diagnostics.store(diagnostic); }
        r.channel->settled = true;
        r.channel->promise.set_value(reply);
    }
    void action(CommandKind kind, core::Tick tick, core::ExternalTarget target = core::ExternalTarget::command,
                core::DigitalState value = core::DigitalState::LOW) {
        journal.push({kind, tick, target, value});
    }
    void collect(core::ConveyorSession& engine) {
        auto batch = engine.drain_result();
        for (auto& snapshot : batch.trace) {
            auto shared = std::make_shared<const core::Snapshot>(std::move(snapshot));
            if (limits.history == HistoryMode::bounded && trace.size() == limits.snapshots) {
                trace.pop_front();
                ++current.discarded_snapshots;
            }
            current.latest = shared;
            trace.push_back(std::move(shared));
        }
        for (const auto& event : batch.events) { events.push(event); }
        for (const auto& event : batch.counter_events) { counter_events.push(event); }
        control = status(engine);
        current.next_tick = engine.next_tick();
    }
    CommandTicket submit_locked(SessionCommand command) {
        auto channel = std::make_shared<Channel>();
        auto future = channel->promise.get_future().share();
        auto code = CommandCode::accepted;
        DiagnosticPtr diagnostic;
        const auto reject = [&](CommandCode failure, DiagnosticReason reason,
                                std::optional<std::uint64_t> used = {}, std::optional<std::uint64_t> limit = {},
                                std::optional<std::uint64_t> requested = {}) {
            code = failure;
            auto value = context(command, DiagnosticStage::admission);
            value.used = used; value.limit = limit; value.requested = requested;
            diagnostic = detail::make_diagnostic(code, reason, std::move(value));
        };
        const bool advancing = command.kind == CommandKind::step || command.kind == CommandKind::advance;
        if (closing) { reject(CommandCode::closed, DiagnosticReason::session_closed); }
        else if (command.session_id != session_id) { reject(CommandCode::stale, DiagnosticReason::wrong_session, {}, {}, command.session_id); }
        else if (command.id == 0 || command.id <= last_id) { reject(CommandCode::stale, DiagnosticReason::stale_id, last_id, {}, command.id); }
        else if (command.kind < CommandKind::start || command.kind > CommandKind::advance) { reject(CommandCode::invalid, DiagnosticReason::invalid_command); }
        else if (command.kind == CommandKind::advance && (command.ticks == 0 || command.ticks > horizon)) {
            reject(CommandCode::invalid, DiagnosticReason::horizon, {}, horizon, command.ticks);
        } else if (command.kind == CommandKind::input && !core::valid(command.value)) { reject(CommandCode::invalid, DiagnosticReason::invalid_value); }
        else if (command.kind == CommandKind::input && command.boundary >= horizon) { reject(CommandCode::invalid, DiagnosticReason::horizon, {}, horizon, command.boundary); }
        else if (command.kind == CommandKind::input && command.target != core::ExternalTarget::command &&
            command.target != core::ExternalTarget::motor_power && command.target != core::ExternalTarget::sensor_power) {
            reject(CommandCode::invalid, DiagnosticReason::invalid_target);
        } else if (stop_requested) { reject(CommandCode::invalid, DiagnosticReason::stop_pending); }
        else if (command.kind == CommandKind::stop) {
            stop_slot = Request{command, channel}; stop_requested = true; last_id = command.id;
        } else if (advancing && advances == limits.advance_requests) { reject(CommandCode::history_limit, DiagnosticReason::advance_budget, advances, limits.advance_requests, 1); }
        else if (command.kind == CommandKind::input && inputs == limits.functional_commands) { reject(CommandCode::history_limit, DiagnosticReason::input_budget, inputs, limits.functional_commands, 1); }
        else if (!advancing && command.kind != CommandKind::input && controls == limits.commands) { reject(CommandCode::history_limit, DiagnosticReason::control_budget, controls, limits.commands, 1); }
        else if (queue.size() == limits.queue_capacity) { reject(CommandCode::queue_full, DiagnosticReason::queue_capacity, queue.size(), limits.queue_capacity, 1); }
        else {
            queue.push_back({command, channel}); last_id = command.id;
            if (advancing) { ++advances; } else if (command.kind == CommandKind::input) { ++inputs; } else { ++controls; }
        }
        CommandReply receipt{command.id, code, 0, 0, 0, diagnostic};
        if (diagnostic) { diagnostics.store(diagnostic); }
        if (code != CommandCode::accepted) { channel->settled = true; channel->promise.set_value(receipt); }
        return {receipt, std::move(future)};
    }
    void run() {
        std::unique_ptr<core::ConveyorSession> engine;
        std::vector<Request> pending;
        std::optional<Request> active, inflight;
        auto cancel_pending = [&] {
            for (const auto& input : pending) { settle(input, CommandCode::cancelled, current.next_tick); }
            pending.clear();
            if (active) { settle(*active, CommandCode::cancelled, active->begin); active.reset(); }
        };
        try {
            for (;;) {
                std::optional<Request> request;
                const bool can_advance = active && current.state == InteractiveState::running;
                {
                    std::unique_lock lock(admission);
                    ready.wait(lock, [&] { return stop_slot || closing || !queue.empty() || can_advance; });
                    // A stop admitted before close always wins; never enters the ordinary queue.
                    if (stop_slot) { request = std::move(stop_slot); stop_slot.reset(); }
                    else if (closing) { break; }
                    else if (!queue.empty()) { request = std::move(queue.front()); queue.pop_front(); }
                }
                std::lock_guard lock(mutex);
                auto& p = current;
                if (request) {
                    inflight = *request;
                    const auto kind = request->command.kind;
                    if (kind == CommandKind::stop) {
                        cancel_pending();
                        if (engine) { engine->stop(); collect(*engine); }
                        // Stop has its own event allowance: <= 2*outputs+2 beyond ordinary budget.
                        action(kind, p.next_tick);
                        if (p.state != InteractiveState::completed) { p.state = InteractiveState::stopped; }
                        engine.reset();
                        {
                            std::lock_guard admission_lock(admission);
                            while (!queue.empty()) { settle(queue.front(), CommandCode::cancelled, p.next_tick); queue.pop_front(); }
                        }
                        publish(); settle(*request, CommandCode::applied, p.next_tick); inflight.reset(); continue;
                    }
                    const bool terminal = p.state == InteractiveState::completed || p.state == InteractiveState::stopped || p.state == InteractiveState::failed;
                    if (terminal) { settle(*request, CommandCode::invalid, p.next_tick); inflight.reset(); continue; }
                    if (kind == CommandKind::input) {
                        if (!engine || request->command.boundary < p.next_tick) {
                            settle(*request, engine ? CommandCode::stale : CommandCode::invalid, p.next_tick,
                                application_diagnostic(*request, engine ? CommandCode::stale : CommandCode::invalid,
                                    engine ? DiagnosticReason::stale_boundary : DiagnosticReason::invalid_state, {}, {}, request->command.boundary));
                        } else { pending.push_back(*request); }
                        inflight.reset(); continue;
                    }
                    const bool advancing = kind == CommandKind::step || kind == CommandKind::advance;
                    const bool allowed = (kind == CommandKind::start && p.state == InteractiveState::loaded) ||
                        (advancing && p.state == InteractiveState::running && !active) ||
                        (kind == CommandKind::pause && p.state == InteractiveState::running) ||
                        (kind == CommandKind::resume && p.state == InteractiveState::paused);
                    if (!allowed) { settle(*request, CommandCode::invalid, p.next_tick, application_diagnostic(*request, CommandCode::invalid,
                        advancing && active ? DiagnosticReason::advance_active : DiagnosticReason::invalid_state)); inflight.reset(); continue; }
                    if (advancing) {
                        const auto requested = kind == CommandKind::step ? 1 : request->command.ticks;
                        if (requested > horizon - p.next_tick) { settle(*request, CommandCode::invalid, p.next_tick,
                            application_diagnostic(*request, CommandCode::invalid, DiagnosticReason::horizon, p.next_tick, horizon, requested)); }
                        else { active = *request; active->begin = p.next_tick; }
                        inflight.reset(); continue;
                    }
                    if (kind != CommandKind::start && retained_events() >= limits.events) {
                        p.diagnostic = application_diagnostic(*request, CommandCode::history_limit, DiagnosticReason::event_budget, retained_events(), limits.events, 1);
                        p.error = CommandCode::history_limit;
                        p.state = InteractiveState::failed; cancel_pending(); publish();
                        settle(*request, CommandCode::history_limit, p.next_tick, p.diagnostic); inflight.reset(); continue;
                    }
                    if (kind == CommandKind::start) {
                        engine = std::make_unique<core::ConveyorSession>(*configuration, false); p.state = InteractiveState::running;
                    } else if (kind == CommandKind::pause) { engine->pause(); p.state = InteractiveState::paused; }
                    else { engine->resume(); p.state = InteractiveState::running; }
                    action(kind, p.next_tick); collect(*engine); publish();
                    settle(*request, CommandCode::applied, p.next_tick); inflight.reset(); continue;
                }
                // At most one complete step between checks of priority stop and ordinary commands.
                const auto boundary = p.next_tick;
                const auto count = static_cast<std::size_t>(std::count_if(pending.begin(), pending.end(),
                    [&](const auto& input) { return input.command.boundary == boundary; }));
                const auto retained = retained_events();
                if ((limits.history == HistoryMode::full && trace.size() == limits.snapshots) ||
                    retained > limits.events || step_records(boundary, count) > limits.events - retained) {
                    const bool snapshots_full = limits.history == HistoryMode::full && trace.size() == limits.snapshots;
                    p.diagnostic = application_diagnostic(*active, CommandCode::history_limit,
                        snapshots_full ? DiagnosticReason::snapshot_budget : DiagnosticReason::event_budget,
                        snapshots_full ? trace.size() : retained, snapshots_full ? limits.snapshots : limits.events,
                        snapshots_full ? 1 : step_records(boundary, count));
                    const auto failed = *active; active.reset();
                    p.state = InteractiveState::failed; p.error = CommandCode::history_limit;
                    cancel_pending(); publish();
                    settle(failed, CommandCode::history_limit, failed.begin, p.diagnostic); continue;
                }
                std::vector<core::FunctionalEvent> values;
                for (const auto& input : pending) {
                    if (input.command.boundary == boundary) { values.push_back({boundary, input.command.id, input.command.target, input.command.value}); }
                }
                engine->step(values);
                for (const auto& input : pending) {
                    if (input.command.boundary == boundary) {
                        action(CommandKind::input, boundary, input.command.target, input.command.value);
                        settle(input, CommandCode::applied, boundary);
                    }
                }
                std::erase_if(pending, [&](const auto& input) { return input.command.boundary == boundary; });
                action(CommandKind::step, boundary); ++active->advanced;
                collect(*engine);
                if (engine->state() == core::SessionState::completed) { p.state = InteractiveState::completed; engine.reset(); }
                publish();
                const auto requested = active->command.kind == CommandKind::step ? 1 : active->command.ticks;
                if (active->advanced == requested) { settle(*active, CommandCode::applied, active->begin); active.reset(); }
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            current.error = CommandCode::execution_error;
            DiagnosticContext value;
            value.session_id = session_id; value.tick = current.next_tick; value.state = current.state;
            if (inflight || active) { value = context(inflight ? inflight->command : active->command, DiagnosticStage::execution, current.next_tick); }
            current.diagnostic = detail::exception_diagnostic(std::current_exception(), std::move(value));
            diagnostics.store(current.diagnostic);
            if (inflight) { settle(*inflight, CommandCode::execution_error, current.next_tick, current.diagnostic); }
        }
        // No functional mutation or replay input is fabricated by cancellation.
        std::lock_guard lock(mutex);
        engine.reset(); cancel_pending();
        {
            std::lock_guard admission_lock(admission);
            closing = true;
            if (stop_slot) { settle(*stop_slot, CommandCode::execution_error, current.next_tick); stop_slot.reset(); }
            while (!queue.empty()) { settle(queue.front(), CommandCode::cancelled, current.next_tick); queue.pop_front(); }
        }
        current.state = InteractiveState::closed; publish(); finished.set_value();
    }
};
InteractiveSession::InteractiveSession(std::uint64_t id, core::RunSpec spec, SessionLimits limits) {
    core::validate(spec);
    if (id == 0 || !spec.conveyor) { throw std::invalid_argument("Interactive session requires ID and conveyor"); }
    data_ = std::make_shared<Data>(id, std::move(spec), checked(limits));
    thread_ = std::jthread([data = data_] { data->run(); });
}
InteractiveSession::InteractiveSession(std::shared_ptr<Runner> runner, std::uint64_t id, core::RunSpec spec, SessionLimits limits)
    : InteractiveSession(id, legacy_configuration(runner, std::move(spec)), limits) {}
InteractiveSession::~InteractiveSession() { close(); }
CommandTicket InteractiveSession::submit(SessionCommand command) {
    std::lock_guard lock(data_->admission);
    auto ticket = data_->submit_locked(command); data_->ready.notify_one(); return ticket;
}
std::vector<CommandTicket> InteractiveSession::submit_batch(const std::vector<SessionCommand>& commands) {
    if (commands.size() > 1024) { throw std::invalid_argument("Batch admission limited to 1024 requests"); }
    std::lock_guard lock(data_->admission);
    std::vector<CommandTicket> tickets; tickets.reserve(commands.size());
    for (const auto& command : commands) { tickets.push_back(data_->submit_locked(command)); }
    data_->ready.notify_one(); return tickets;
}
std::shared_ptr<const SessionPublication> InteractiveSession::publication() const {
    std::lock_guard lock(data_->publication_mutex); return data_->published;
}
DiagnosticPtr InteractiveSession::diagnostic() const { return data_->diagnostics.load(); }
std::shared_ptr<const SessionHistoryView> InteractiveSession::history_view() const {
    auto view = std::make_shared<SessionHistoryView>();
    view->configuration = data_->configuration;
    view->limits = data_->limits;
    std::lock_guard lock(data_->mutex);
    view->publication = data_->current;
    view->trace.assign(data_->trace.begin(), data_->trace.end());
    view->events = data_->events.share();
    view->counter_events = data_->counter_events.share();
    view->control = data_->control;
    view->commands = data_->commands.share();
    view->applied = data_->journal.share();
    return view;
}
SnapshotPage InteractiveSession::snapshots(core::Tick first_tick, std::size_t max_count) const {
    if (max_count == 0 || max_count > max_snapshot_page) { throw std::invalid_argument("Snapshot page size must be in [1, 4096]"); }
    SnapshotPage page;
    std::lock_guard lock(data_->mutex);
    const auto& trace = data_->trace;
    page.discarded_snapshots = data_->current.discarded_snapshots;
    if (trace.empty()) { return page; }
    page.first_tick = trace.front()->tick;
    page.last_tick = trace.back()->tick;
    if (first_tick > *page.last_tick) { return page; }
    const auto begin = first_tick > *page.first_tick ? static_cast<std::size_t>(first_tick - *page.first_tick) : std::size_t{};
    const auto count = std::min(max_count, trace.size() - begin);
    const auto start = trace.begin() + static_cast<std::ptrdiff_t>(begin);
    page.snapshots.assign(start, start + static_cast<std::ptrdiff_t>(count));
    return page;
}
SessionHistory materialize(const SessionHistoryView& view) {
    SessionHistory history;
    history.publication = view.publication;
    history.result.trace.reserve(view.trace.size());
    for (const auto& snapshot : view.trace) { history.result.trace.push_back(*snapshot); }
    history.result.events = view.events.flatten();
    history.result.counter_events = view.counter_events.flatten();
    history.result.control = view.control;
    history.commands = view.commands.flatten();
    history.replay = std::make_shared<const SessionReplay>(SessionReplay{*view.configuration, view.limits, view.applied.flatten()});
    return history;
}
std::shared_ptr<const SessionHistory> InteractiveSession::history() const {
    const auto view = history_view();
    return std::make_shared<const SessionHistory>(materialize(*view));
}
std::shared_future<void> InteractiveSession::request_close() {
    std::lock_guard lock(data_->admission);
    data_->closing = true; data_->ready.notify_one(); return data_->completion;
}
void InteractiveSession::close() {
    std::lock_guard lock(close_mutex_);
    request_close();
    if (thread_.joinable()) { thread_.join(); }
}
core::RunResult replay_session(const SessionReplay& replay) {
    core::validate(replay.configuration); const auto limits = checked(replay.limits);
    if (!replay.configuration.conveyor || replay.applied.size() > replay.configuration.ticks + limits.functional_commands + limits.commands + 1) {
        throw std::invalid_argument("Invalid session replay envelope");
    }
    std::unique_ptr<core::ConveyorSession> engine;
    core::RunResult result;
    std::deque<core::Snapshot> trace;
    std::vector<core::FunctionalEvent> pending;
    bool started{}, stopped{};
    std::size_t controls{}, inputs{};
    for (const auto& action : replay.applied) {
        const auto boundary = engine ? engine->next_tick() : 0;
        if (action.tick != boundary || stopped) { throw std::invalid_argument("Invalid replay boundary/order"); }
        switch (action.kind) {
        case CommandKind::start:
            if (started || !pending.empty()) { throw std::invalid_argument("Duplicate replay start"); }
            engine = std::make_unique<core::ConveyorSession>(replay.configuration, false); started = true; ++controls; break;
        case CommandKind::input:
            if (!engine || engine->state() != core::SessionState::running || ++inputs > limits.functional_commands || !core::valid(action.value) ||
                (action.target != core::ExternalTarget::command && action.target != core::ExternalTarget::motor_power && action.target != core::ExternalTarget::sensor_power)) {
                throw std::invalid_argument("Invalid applied replay input");
            }
            pending.push_back({boundary, static_cast<std::uint64_t>(inputs), action.target, action.value}); break;
        case CommandKind::step:
            if (!engine || engine->state() != core::SessionState::running ||
                (limits.history == HistoryMode::full && trace.size() >= limits.snapshots)) { throw std::invalid_argument("Invalid replay step"); }
            engine->step(pending); pending.clear(); break;
        case CommandKind::pause:
            if (!engine || engine->state() != core::SessionState::running || !pending.empty()) { throw std::invalid_argument("Invalid replay pause"); }
            engine->pause(); ++controls; break;
        case CommandKind::resume:
            if (!engine || engine->state() != core::SessionState::paused || !pending.empty()) { throw std::invalid_argument("Invalid replay resume"); }
            engine->resume(); ++controls; break;
        case CommandKind::stop:
            if (!pending.empty()) { throw std::invalid_argument("Unapplied replay inputs before stop"); }
            if (engine) { engine->stop(); } stopped = true; break;
        case CommandKind::advance: throw std::invalid_argument("Replay must contain applied individual steps");
        default: throw std::invalid_argument("Invalid replay action");
        }
        if (controls > limits.commands) { throw std::invalid_argument("Replay control limit"); }
        if (engine) {
            auto batch = engine->drain_result();
            for (auto& snapshot : batch.trace) {
                if (limits.history == HistoryMode::bounded && trace.size() == limits.snapshots) { trace.pop_front(); }
                trace.push_back(std::move(snapshot));
            }
            result.events.insert(result.events.end(), batch.events.begin(), batch.events.end());
            result.counter_events.insert(result.counter_events.end(), batch.counter_events.begin(), batch.counter_events.end());
            result.control = status(*engine);
        }
        const auto allowance = action.kind == CommandKind::stop ? 2ULL * replay.configuration.program.output_count + 2 : 0;
        if (result.events.size() + result.counter_events.size() > limits.events + allowance) { throw std::invalid_argument("Replay event limit"); }
    }
    if (!pending.empty()) { throw std::invalid_argument("Replay contains unapplied input"); }
    result.trace.assign(std::make_move_iterator(trace.begin()), std::make_move_iterator(trace.end()));
    return result;
}
}
