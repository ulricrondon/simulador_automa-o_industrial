#include "session_controller.hpp"
#include <algorithm>
#include <chrono>
#include <utility>

namespace sim::desktop {
namespace {
using runtime::CommandCode;
using runtime::CommandKind;
using runtime::InteractiveState;
using namespace std::chrono_literals;
template<class T> bool ready(const T& future) { return future.wait_for(0s) == std::future_status::ready; }
template<class F> auto launch(const std::function<void()>& before_task, F task) {
    if (before_task) { before_task(); }
    return std::async(std::launch::async, std::move(task));
}
}
SessionController::SessionController(runtime::SessionLimits limits, std::function<void()> before_task)
    : limits_(limits), before_task_(std::move(before_task)) {}
void SessionController::open(std::filesystem::path path) {
    if (!idle() || shutting_down_) { return; }
    try {
        // Validation can take noticeable time on a large document. The task owns only its path.
        loading_ = launch(before_task_, [file = path] { return std::make_shared<const project::Document>(project::load(file)); });
        loading_path_ = std::move(path);
    } catch (const std::exception& error) {
        report_local("Não foi possível abrir o projeto", error.what());
    }
}
void SessionController::start() {
    if (!idle() || shutting_down_ || !document_) { return; }
    if (!document_->simulation.conveyor) {
        report_local("Projeto sem sessão interativa", "A API interativa executa somente projetos com esteira. Este projeto foi validado, mas não pode ser iniciado aqui.");
        return;
    }
    const auto id = session_id_ + 1;
    try {
        // Copying and validating the RunSpec grows with the project: never on the caller's thread.
        starting_ = launch(before_task_, [document = document_, id, limits = limits_] {
            return std::make_unique<runtime::InteractiveSession>(id, document->simulation, limits);
        });
    } catch (const std::exception& error) {
        report_local("Não foi possível criar a sessão", error.what());
        return;
    }
    session_id_ = id;
    command_id_ = 0;
    failure_.reset();
    reset_visuals();
}
void SessionController::step() { submit({session_id_, 0, CommandKind::step}); }
void SessionController::advance(core::Tick ticks) {
    runtime::SessionCommand command{session_id_, 0, CommandKind::advance};
    command.ticks = ticks;
    submit(command);
}
void SessionController::pause() { submit({session_id_, 0, CommandKind::pause}); }
void SessionController::resume() { submit({session_id_, 0, CommandKind::resume}); }
// Uses the reserved stop position of the API; a full ordinary queue cannot reject it.
void SessionController::stop() { submit({session_id_, 0, CommandKind::stop}); }
void SessionController::submit(runtime::SessionCommand command) {
    if (!session_ || closing_) { return; }
    command.id = ++command_id_; // The only place that assigns command IDs.
    try {
        auto ticket = session_->submit(command);
        if (ticket.acceptance.code == CommandCode::accepted) {
            pending_.push_back({command.kind, std::move(ticket.completion)});
        } else {
            observe(ticket.acceptance.diagnostic); // Its completion carries the same diagnostic.
        }
    } catch (const std::exception& error) {
        report_local("Comando não enviado", error.what());
    }
}
void SessionController::close_session() {
    if (!session_ || closing_) { return; }
    close_done_ = session_->request_close();
    closing_ = true;
}
void SessionController::request_shutdown() {
    shutting_down_ = true;
    close_session();
}
void SessionController::report_local(std::string title, std::string message) {
    FeedEntry entry;
    entry.severity = Severity::fatal;
    entry.title = std::move(title);
    entry.message = std::move(message);
    entry.session_id = session_id_;
    add_entry(std::move(entry));
}
void SessionController::add_entry(FeedEntry entry) {
    if (entry.severity == Severity::fatal) { undelivered_alerts_.push_back(entry); }
    undelivered_.push_back(entry);
    feed_.push_back(std::move(entry));
    if (feed_.size() > max_feed_entries) { feed_.pop_front(); }
}
void SessionController::observe(const runtime::DiagnosticPtr& diagnostic) {
    if (!diagnostic) { return; }
    // The same diagnostic reaches us by ticket and by the publication.
    // The failure check covers a cause that has already left the bounded feed.
    const auto seen = std::any_of(feed_.begin(), feed_.end(), [&](const auto& entry) { return entry.diagnostic == diagnostic; });
    if (seen || (failure_ && failure_->diagnostic == diagnostic)) { return; }
    const bool cause = observation_ && observation_->publication.diagnostic == diagnostic;
    FeedEntry entry;
    entry.severity = classify(*diagnostic, cause);
    entry.title = reason_label(diagnostic->reason);
    entry.message = diagnostic->message;
    entry.context = context_text(diagnostic->context);
    entry.session_id = diagnostic->context.session_id;
    entry.diagnostic = diagnostic;
    add_entry(std::move(entry));
}
// Everything shown belongs to one session: a new one starts from absent values, not old ones.
void SessionController::reset_visuals() {
    observation_.reset();
    cursor_ = {session_id_, 0};
    lost_events_ = 0;
    events_.clear();
    events_dirty_ = events_reset_ = true;
    events_added_ = 0;
    state_panel_ = make_state_panel(nullptr, horizon(), std::nullopt, configuration());
    signal_panel_ = make_signal_panel(nullptr, configuration());
    state_dirty_ = signals_dirty_ = true;
}
void SessionController::refresh_project(PollResult& result) {
    if (!loading_.valid() || !ready(loading_)) { return; }
    if (shutting_down_) { // Nobody is left to read the result, or its failure.
        try { loading_.get(); } catch (...) {}
        return;
    }
    try {
        document_ = loading_.get();
        const auto& simulation = document_->simulation;
        project_ = ProjectInfo{document_->title, document_->project_id, loading_path_, simulation.ticks, simulation.tick_period_us, simulation.conveyor.has_value()};
        failure_.reset();
        reset_visuals();
        result.project_changed = true;
    } catch (const project::Error& error) {
        report_local(std::string(project_error_label(error.code())), error.what());
    } catch (const std::exception& error) {
        report_local("Não foi possível abrir o projeto", error.what());
    } catch (...) {
        report_local("Não foi possível abrir o projeto", "Erro desconhecido.");
    }
}
void SessionController::refresh_start() {
    if (!starting_.valid() || !ready(starting_)) { return; }
    try {
        session_ = starting_.get();
        closing_ = false;
    } catch (const std::exception& error) {
        report_local("Não foi possível criar a sessão", error.what());
        return;
    } catch (...) {
        report_local("Não foi possível criar a sessão", "Erro desconhecido.");
        return;
    }
    if (!shutting_down_) { submit({session_id_, 0, CommandKind::start}); }
}
void SessionController::refresh_observation() {
    auto current = session_->observation();
    if (current == observation_) { return; }
    const auto* previous_image = observation_ ? observation_->publication.latest.get() : nullptr;
    if (current->publication.latest.get() != previous_image) { signals_dirty_ = true; }
    observation_ = std::move(current); // Releases the previous boundary.
}
void SessionController::consume_events() {
    if (!observation_ || cursor_.next_sequence >= observation_->events->next_sequence) { return; }
    // One page reaches the end: the window never retains more than the largest page.
    auto page = runtime::observation_events(*observation_, cursor_, runtime::observation_block_size * runtime::observation_max_blocks);
    order_events(page.events);
    std::vector<EventRow> rows;
    const auto capacity = max_event_rows - (page.lost ? 1 : 0);
    const auto shown = std::min(page.events.size(), capacity);
    rows.reserve(shown + 1);
    if (page.lost) { rows.push_back(lost_row(page.lost, page.events.empty() ? core::Tick{} : page.events.front().tick)); }
    for (auto i = page.events.size() - shown; i < page.events.size(); ++i) { rows.push_back(event_row(page.events[i])); }
    // Nothing above changed our state: on failure the next poll retries from the same cursor.
    events_added_ = std::min(max_event_rows, events_added_ + rows.size());
    events_.insert(events_.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    while (events_.size() > max_event_rows) { events_.pop_front(); }
    lost_events_ += page.lost;
    cursor_ = page.next;
    events_dirty_ = true;
}
PollResult SessionController::poll() {
    PollResult result;
    refresh_project(result);
    refresh_start();
    // After close completes every ticket is settled and the final observation is published.
    const bool finished = session_ && closing_ && ready(close_done_);
    if (session_) { refresh_observation(); }
    consume_events();
    std::vector<runtime::DiagnosticPtr> replies;
    std::erase_if(pending_, [&](const Pending& item) {
        if (!ready(item.completion)) { return false; }
        replies.push_back(item.completion.get().diagnostic);
        return true;
    });
    for (const auto& diagnostic : replies) { observe(diagnostic); }
    if (session_) {
        const auto& publication = observation_->publication;
        observe(publication.diagnostic);
        if (publication.diagnostic) {
            const auto it = std::find_if(feed_.begin(), feed_.end(), [&](const auto& entry) { return entry.diagnostic == publication.diagnostic; });
            if (it != feed_.end() && (!failure_ || failure_->diagnostic != publication.diagnostic)) { failure_ = *it; }
        }
        // The session can end by itself after an execution error. Also retries a refused request.
        if (shutting_down_ || publication.state == InteractiveState::closed) { close_session(); }
        if (finished) {
            session_.reset();
            closing_ = false;
        }
    }
    const auto* publication = this->publication();
    ControlInput input;
    input.loading = loading_.valid() || starting_.valid();
    input.project_loaded = document_ != nullptr;
    input.interactive_supported = project_ && project_->interactive_supported;
    input.session_active = session_ != nullptr;
    input.closing = closing_;
    input.shutting_down = shutting_down_;
    if (publication) { input.state = publication->state; input.next_tick = publication->next_tick; }
    input.horizon = horizon();
    input.advance_in_flight = std::any_of(pending_.begin(), pending_.end(), [](const auto& item) { return item.kind == CommandKind::step || item.kind == CommandKind::advance; });
    input.stop_in_flight = std::any_of(pending_.begin(), pending_.end(), [](const auto& item) { return item.kind == CommandKind::stop; });
    const auto enabled = compute_controls(input);
    result.controls_changed = !(enabled == controls_);
    controls_ = enabled;
    std::optional<core::SessionStatus> control;
    if (observation_) { control = observation_->control; }
    auto state = make_state_panel(publication, input.horizon, control, configuration(), lost_events_);
    if (starting_.valid()) { state.state = "Iniciando sessão"; }
    result.state_changed = std::exchange(state_dirty_, false) || !(state == state_panel_);
    state_panel_ = std::move(state);
    if (std::exchange(signals_dirty_, false)) {
        signal_panel_ = make_signal_panel(publication ? publication->latest.get() : nullptr, configuration());
        result.signals_changed = true;
    }
    result.events_changed = std::exchange(events_dirty_, false);
    result.events_reset = std::exchange(events_reset_, false);
    result.events_added = std::exchange(events_added_, 0);
    result.new_entries = std::exchange(undelivered_, {});
    result.alerts = std::exchange(undelivered_alerts_, {});
    return result;
}
}
