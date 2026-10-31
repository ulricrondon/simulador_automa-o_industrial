#pragma once
#include "sim/project/document.hpp"
#include "sim/runtime/interactive_session.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Qt-free presentation rules: pure functions over the immutable data published by the runtime.
namespace sim::desktop {
inline constexpr std::string_view neutral_value = "—";

std::string_view state_label(runtime::InteractiveState state);
std::string_view command_label(runtime::CommandKind kind);
std::string_view code_label(runtime::CommandCode code);
std::string_view reason_label(runtime::DiagnosticReason reason);
std::string_view stage_label(runtime::DiagnosticStage stage);
std::string_view project_error_label(project::ErrorCode code);

enum class Severity { fatal, recoverable, secondary };
std::string_view severity_label(Severity severity);
// Decided only from typed values, never from the message text.
Severity classify(const runtime::SessionDiagnostic& diagnostic, bool causes_failure);
// Only the fields present in the context; a command rejected at admission has no tick.
std::string context_text(const runtime::DiagnosticContext& context);

struct ControlInput {
    bool loading{};
    bool project_loaded{};
    bool interactive_supported{};
    bool session_active{};
    bool closing{};
    bool shutting_down{};
    std::optional<runtime::InteractiveState> state;
    bool advance_in_flight{};
    bool stop_in_flight{};
    core::Tick next_tick{};
    core::Tick horizon{};
};
struct ControlState {
    bool open{}, start{}, step{}, advance{}, pause{}, resume{}, stop{}, close{};
    bool operator==(const ControlState&) const = default;
};
ControlState compute_controls(const ControlInput& input);

struct Row {
    std::string name;
    std::string value;
    bool operator==(const Row&) const = default;
};
struct StatePanel {
    std::string state{neutral_value};
    std::string tick{neutral_value}; // Next tick to run, over the project horizon.
    std::string image_tick{neutral_value}; // Tick of the displayed end-of-step image.
    std::string position{neutral_value};
    std::string control_motor{neutral_value}; // After controls: stop has no new image.
    std::string discarded{neutral_value};
    std::string lost_events{neutral_value}; // Left the observation window before being shown.
    std::uint64_t sequence{};
    bool operator==(const StatePanel&) const = default;
};
StatePanel make_state_panel(const runtime::SessionPublication* publication, core::Tick horizon,
                            const std::optional<core::SessionStatus>& control, const core::RunSpec* configuration,
                            std::uint64_t lost_events = 0);

struct SignalPanel {
    std::vector<Row> inputs, outputs, plant, blocks;
    bool operator==(const SignalPanel&) const = default;
};
// Null snapshot gives neutral rows for what the project declares; nothing is estimated.
SignalPanel make_signal_panel(const core::Snapshot* snapshot, const core::RunSpec* configuration);

struct EventRow {
    core::Tick tick{};
    std::string text;
    bool lost{}; // Marks a gap, not an event.
    bool operator==(const EventRow&) const = default;
};
EventRow event_row(const core::Transition& event);
EventRow event_row(const core::CounterEvent& event);
EventRow event_row(const runtime::ObservationEvent& event);
EventRow lost_row(std::uint64_t count, core::Tick next_tick);
// Full key of the observation contract: boundary, time, tick, phase, phase order, sequence.
void order_events(std::vector<runtime::ObservationEvent>& events);
}
