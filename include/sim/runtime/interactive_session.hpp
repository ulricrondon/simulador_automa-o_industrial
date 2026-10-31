#pragma once
#include "sim/runtime/runner.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <variant>

namespace sim::runtime {
enum class InteractiveState { loaded, running, paused, completed, stopped, closed, failed };
enum class CommandKind { start, step, pause, resume, stop, input, advance };
enum class CommandCode { accepted, applied, invalid, stale, queue_full, closed, history_limit, cancelled, execution_error };
enum class HistoryMode { full, bounded };
struct SessionLimits {
    std::size_t queue_capacity{32};
    HistoryMode history{HistoryMode::bounded};
    std::size_t snapshots{256};
    std::size_t events{100000}; // Combined transition and counter records; never evicted.
    std::size_t commands{10000}; // Ordinary control admissions (start/pause/resume).
    std::size_t functional_commands{10000};
    std::size_t advance_requests{100000}; // Separate from controls and functional inputs.
};
struct SessionCommand {
    std::uint64_t session_id{};
    std::uint64_t id{}; // Strictly increasing per session; rejected IDs may be retried.
    CommandKind kind{CommandKind::step};
    core::Tick boundary{}; // Required for functional input; rejected if already obsolete.
    core::ExternalTarget target{core::ExternalTarget::command};
    core::DigitalState value{core::DigitalState::LOW};
    core::Tick ticks{1}; // Only advance: [1, configuration.ticks].
};
enum class DiagnosticReason {
    session_closed, wrong_session, stale_id, invalid_command, invalid_value, invalid_target,
    horizon, stop_pending, queue_capacity, control_budget, input_budget, advance_budget,
    snapshot_budget, event_budget, invalid_state, stale_boundary, advance_active,
    cancelled, known_exception, unknown_exception, allocation_failure
};
enum class DiagnosticStage { admission, application, execution };
struct DiagnosticContext {
    std::uint64_t session_id{};
    std::optional<std::uint64_t> command_id;
    std::optional<CommandKind> command;
    std::optional<core::Tick> tick; // Absent at admission: no fabricated application tick.
    std::optional<InteractiveState> state;
    std::optional<std::uint64_t> used, limit, requested;
    DiagnosticStage stage{DiagnosticStage::admission};
};
struct SessionDiagnostic {
    CommandCode code{};
    DiagnosticReason reason{};
    std::string message; // Bounded, plain text; presentation must not interpret as markup.
    DiagnosticContext context;
};
using DiagnosticPtr = std::shared_ptr<const SessionDiagnostic>;
struct CommandReply {
    std::uint64_t id{};
    CommandCode code{};
    core::Tick tick{};
    std::uint64_t application_sequence{};
    core::Tick advanced_ticks{}; // Includes a partially cancelled advance.
    DiagnosticPtr diagnostic{};
};
struct CommandTicket {
    CommandReply acceptance;
    std::shared_future<CommandReply> completion;
};
struct CommandRecord {
    SessionCommand command;
    CommandReply reply;
};
struct SessionPublication {
    std::uint64_t session_id{};
    std::uint64_t sequence{}; // Monotonic publication revision (includes controls).
    InteractiveState state{InteractiveState::loaded};
    core::Tick next_tick{};
    // Last complete end-of-step image, never a partial/control-phase image.
    std::shared_ptr<const core::Snapshot> latest;
    std::optional<core::Tick> first_tick, last_tick;
    std::size_t discarded_snapshots{};
    CommandCode error{CommandCode::applied};
    DiagnosticPtr diagnostic{}; // Failure causing failed/closed; retained across cancellation.
};
// Observation storage is bounded independently of the durable functional event log.
inline constexpr std::size_t observation_block_size = 256;
inline constexpr std::size_t observation_max_blocks = 32; // Nominal capacity: 8192 records.
// Two partial endpoints may require one extra physical block, without extra records.
inline constexpr std::size_t observation_storage_blocks = observation_max_blocks + 1;
struct ObservationEvent {
    std::uint64_t sequence{}; // Contiguous event cursor, starting at zero per session.
    std::uint64_t publication_sequence{}; // Functional boundary; orders controls without ticks.
    core::Tick tick{};
    std::uint64_t time_us{};
    core::Phase phase{};
    std::uint32_t phase_order{}; // Counter/output ordering within the phase.
    std::variant<core::Transition, core::CounterEvent> value;
};
struct ObservationEvents {
    std::vector<std::shared_ptr<const std::vector<ObservationEvent>>> blocks;
    std::uint64_t first_sequence{}, next_sequence{}; // Half-open retained range.
};
struct SessionObservation {
    SessionPublication publication;
    std::optional<core::SessionStatus> control;
    std::shared_ptr<const ObservationEvents> events;
};
struct ObservationCursor {
    std::uint64_t session_id{}, next_sequence{};
};
struct ObservationPage {
    std::vector<ObservationEvent> events;
    ObservationCursor next;
    std::uint64_t lost{}; // Requested records evicted from the presentation window.
};
// Pure bounded paging of ONE retained observation. Wrong-session/future cursors throw.
ObservationPage observation_events(const SessionObservation& observation, ObservationCursor cursor,
                                   std::size_t max_count = observation_block_size);
struct ReplayAction {
    CommandKind kind{};
    core::Tick tick{};
    core::ExternalTarget target{core::ExternalTarget::command};
    core::DigitalState value{core::DigitalState::LOW};
};
struct SessionReplay {
    core::RunSpec configuration;
    SessionLimits limits;
    std::vector<ReplayAction> applied; // Actual execution order, never cancelled requests.
};
// Offline synchronous replay; call on a worker, not on a graphical event thread.
core::RunResult replay_session(const SessionReplay& replay);
struct SessionHistory {
    core::RunResult result;
    std::vector<CommandRecord> commands;
    SessionPublication publication;
    std::shared_ptr<const SessionReplay> replay;
};
// Ordered records in immutable shared blocks; holding them never blocks the session.
template<class T> struct HistorySegments {
    std::vector<std::shared_ptr<const std::vector<T>>> chunks;
    std::size_t size{};
    std::vector<T> flatten() const {
        std::vector<T> values;
        values.reserve(size);
        for (const auto& chunk : chunks) { values.insert(values.end(), chunk->begin(), chunk->end()); }
        return values;
    }
};
// Coherent view without deep copies. It pins what it references until released.
struct SessionHistoryView {
    SessionPublication publication;
    std::vector<std::shared_ptr<const core::Snapshot>> trace; // Consecutive ascending ticks.
    HistorySegments<core::Transition> events;
    HistorySegments<core::CounterEvent> counter_events;
    std::optional<core::SessionStatus> control;
    HistorySegments<CommandRecord> commands;
    std::shared_ptr<const core::RunSpec> configuration;
    SessionLimits limits;
    HistorySegments<ReplayAction> applied;
};
inline constexpr std::size_t max_snapshot_page = 4096;
struct SnapshotPage {
    std::vector<std::shared_ptr<const core::Snapshot>> snapshots;
    std::optional<core::Tick> first_tick, last_tick; // Retained range at query time.
    std::size_t discarded_snapshots{};
};
// Deep copy of a view; call on a worker for large histories.
SessionHistory materialize(const SessionHistoryView& view);
class InteractiveSession {
public:
    InteractiveSession(std::uint64_t session_id, core::RunSpec configuration, SessionLimits limits = {});
    // Compatibility overload: the supplied Runner is not occupied or retained.
    InteractiveSession(std::shared_ptr<Runner> runner, std::uint64_t session_id,
                       core::RunSpec configuration, SessionLimits limits = {});
    ~InteractiveSession();
    InteractiveSession(const InteractiveSession&) = delete;
    InteractiveSession& operator=(const InteractiveSession&) = delete;
    CommandTicket submit(SessionCommand command);
    std::vector<CommandTicket> submit_batch(const std::vector<SessionCommand>& commands);
    // Nonblocking cancellation request; completion becomes ready after teardown.
    std::shared_future<void> request_close();
    std::shared_ptr<const SessionPublication> publication() const;
    // Latest rejection/failure; independent of step/history locks. Null before any diagnostic.
    DiagnosticPtr diagnostic() const;
    // Constant-size pointer acquisition; never takes the functional/history mutex.
    std::shared_ptr<const SessionObservation> observation() const;
    // Deep copy made outside session locks: expensive for the caller, not for the session.
    std::shared_ptr<const SessionHistory> history() const;
    std::shared_ptr<const SessionHistoryView> history_view() const;
    // Up to max_count snapshots from first_tick, or from the first retained tick if later.
    SnapshotPage snapshots(core::Tick first_tick, std::size_t max_count) const;
    // Cancels queued/pending commands, wakes a paused session, and waits for teardown.
    // Serialized internally; object destruction still requires callers to relinquish it.
    void close();
private:
    struct Data;
    std::shared_ptr<Data> data_;
    std::jthread thread_;
    std::mutex close_mutex_;
};
}
