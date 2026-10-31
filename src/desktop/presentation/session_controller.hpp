#pragma once
#include "view_model.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sim::desktop {
struct FeedEntry {
    Severity severity{Severity::recoverable};
    std::string title;   // Typed label, from reason or project error code.
    std::string message; // Plain text; never interpret as markup.
    std::string context;
    std::uint64_t session_id{};
    runtime::DiagnosticPtr diagnostic; // Empty for errors raised outside a session.
};
struct PollResult {
    bool project_changed{};
    bool state_changed{};
    bool signals_changed{};
    bool controls_changed{};
    bool events_changed{};
    bool events_reset{};         // Drop the rows shown so far, then add events_added.
    std::size_t events_added{};  // Rows at the end of events() not delivered yet.
    std::vector<FeedEntry> new_entries;
    std::vector<FeedEntry> alerts; // Fatal entries, each delivered once.
};
struct ProjectInfo {
    std::string title;
    std::string project_id;
    std::filesystem::path path;
    core::Tick horizon{};
    std::uint64_t tick_period_us{};
    bool interactive_supported{};
};
// Single-thread adapter between a GUI event loop and InteractiveSession. No method waits for
// the session thread; poll() gathers whatever is ready. Tear down only once idle() is true.
class SessionController {
public:
    static constexpr std::size_t max_event_rows = 200;
    static constexpr std::size_t max_feed_entries = 256;
    // before_task runs right before each std::async; a test throws there what std::async can raise.
    explicit SessionController(runtime::SessionLimits limits = {}, std::function<void()> before_task = {});
    void open(std::filesystem::path path);
    void start();
    void step();
    void advance(core::Tick ticks);
    void pause();
    void resume();
    void stop();
    void close_session();
    void request_shutdown();
    PollResult poll();
    bool idle() const noexcept { return !session_ && !loading_.valid() && !starting_.valid(); }
    bool session_active() const noexcept { return session_ != nullptr; }
    bool shutting_down() const noexcept { return shutting_down_; }
    const std::optional<ProjectInfo>& project() const noexcept { return project_; }
    const ControlState& controls() const noexcept { return controls_; }
    const StatePanel& state_panel() const noexcept { return state_panel_; }
    const SignalPanel& signal_panel() const noexcept { return signal_panel_; }
    const std::deque<EventRow>& events() const noexcept { return events_; }
    const std::deque<FeedEntry>& feed() const noexcept { return feed_; }
    // Cause of the last failure; kept after the session closes, until another session starts.
    const std::optional<FeedEntry>& failure() const noexcept { return failure_; }
    // The one observation behind every panel; replaced, not accumulated.
    const std::shared_ptr<const runtime::SessionObservation>& observation() const noexcept { return observation_; }
    const runtime::SessionPublication* publication() const noexcept { return observation_ ? &observation_->publication : nullptr; }
private:
    struct Pending { runtime::CommandKind kind{}; std::shared_future<runtime::CommandReply> completion; };
    void submit(runtime::SessionCommand command);
    void report_local(std::string title, std::string message);
    void add_entry(FeedEntry entry);
    void observe(const runtime::DiagnosticPtr& diagnostic);
    void refresh_project(PollResult& result);
    void refresh_start();
    void refresh_observation();
    void consume_events();
    void reset_visuals();
    core::Tick horizon() const noexcept { return project_ ? project_->horizon : 0; }
    const core::RunSpec* configuration() const noexcept { return document_ ? &document_->simulation : nullptr; }
    runtime::SessionLimits limits_;
    std::function<void()> before_task_;
    std::future<std::shared_ptr<const project::Document>> loading_;
    std::filesystem::path loading_path_;
    std::shared_ptr<const project::Document> document_;
    std::optional<ProjectInfo> project_;
    std::future<std::unique_ptr<runtime::InteractiveSession>> starting_;
    std::unique_ptr<runtime::InteractiveSession> session_;
    std::shared_future<void> close_done_;
    bool closing_{}, shutting_down_{};
    std::uint64_t session_id_{}, command_id_{};
    std::vector<Pending> pending_;
    std::shared_ptr<const runtime::SessionObservation> observation_; // Pins one boundary only.
    runtime::ObservationCursor cursor_;
    std::uint64_t lost_events_{};
    ControlState controls_;
    StatePanel state_panel_;
    SignalPanel signal_panel_;
    bool state_dirty_{}, signals_dirty_{};
    std::deque<EventRow> events_;
    bool events_dirty_{}, events_reset_{};
    std::size_t events_added_{};
    std::deque<FeedEntry> feed_;
    std::optional<FeedEntry> failure_;
    std::vector<FeedEntry> undelivered_, undelivered_alerts_;
};
}
