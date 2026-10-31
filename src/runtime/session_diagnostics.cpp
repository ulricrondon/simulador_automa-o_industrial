#include "session_diagnostics.hpp"
#include <new>
#include <string_view>
namespace sim::runtime::detail {
namespace {
std::string_view message(DiagnosticReason reason) {
    switch (reason) {
    case DiagnosticReason::session_closed: return "Session is closed; no command was accepted.";
    case DiagnosticReason::wrong_session: return "Command belongs to a different session.";
    case DiagnosticReason::stale_id: return "Command ID must be nonzero and newer than the last accepted ID.";
    case DiagnosticReason::invalid_command: return "Command kind is not supported.";
    case DiagnosticReason::invalid_value: return "Input value is not a valid digital state.";
    case DiagnosticReason::invalid_target: return "Input target is not supported.";
    case DiagnosticReason::horizon: return "Requested time is outside the configured simulation horizon.";
    case DiagnosticReason::stop_pending: return "Stop has already been requested; no further command was accepted.";
    case DiagnosticReason::queue_capacity: return "Command queue is full; request was not accepted.";
    case DiagnosticReason::control_budget: return "Control command admission limit reached.";
    case DiagnosticReason::input_budget: return "Functional input admission limit reached.";
    case DiagnosticReason::advance_budget: return "Time advance request limit reached.";
    case DiagnosticReason::snapshot_budget: return "Full snapshot history limit reached; next step was not executed.";
    case DiagnosticReason::event_budget: return "Event history has insufficient capacity for this operation.";
    case DiagnosticReason::invalid_state: return "Command is not valid in the current session state.";
    case DiagnosticReason::stale_boundary: return "Input boundary has already passed; input was not applied.";
    case DiagnosticReason::advance_active: return "A time advance is already active.";
    case DiagnosticReason::cancelled: return "Request or remaining advance was cancelled before application.";
    case DiagnosticReason::known_exception: return "Session operation failed.";
    case DiagnosticReason::allocation_failure: return "Insufficient memory to complete the session operation.";
    case DiagnosticReason::unknown_exception: return "Unexpected session failure.";
    }
    return "Session operation failed.";
}
std::string bounded_message(const char* text) {
    std::string result;
    if (!text) { return result; }
    while (*text && result.size() < diagnostic_message_limit) {
        const auto c = static_cast<unsigned char>(*text++);
        result.push_back(c < 32 || c == 127 ? ' ' : static_cast<char>(c));
    }
    // Do not split a UTF-8 code point at the byte limit.
    if (*text && !result.empty()) {
        auto start = result.size() - 1;
        while (start > 0 && (static_cast<unsigned char>(result[start]) & 0xc0) == 0x80) { --start; }
        const auto lead = static_cast<unsigned char>(result[start]);
        const std::size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (start + width > result.size()) { result.resize(start); }
    }
    return result;
}
}
DiagnosticPtr make_diagnostic(CommandCode code, DiagnosticReason reason, DiagnosticContext context) {
    return std::make_shared<const SessionDiagnostic>(SessionDiagnostic{code, reason, std::string(message(reason)), std::move(context)});
}
DiagnosticPtr exception_diagnostic(std::exception_ptr exception, DiagnosticContext context) {
    context.stage = DiagnosticStage::execution;
    try {
        if (exception) { std::rethrow_exception(exception); }
    } catch (const std::bad_alloc&) {
        return make_diagnostic(CommandCode::execution_error, DiagnosticReason::allocation_failure, std::move(context));
    } catch (const std::future_error&) {
        return make_diagnostic(CommandCode::execution_error, DiagnosticReason::unknown_exception, std::move(context));
    } catch (const std::length_error&) {
        return make_diagnostic(CommandCode::execution_error, DiagnosticReason::unknown_exception, std::move(context));
    } catch (const std::logic_error& error) {
        // Known validation/state exceptions contain domain messages, not platform internals.
        auto text = bounded_message(error.what());
        if (text.empty()) { text = std::string(message(DiagnosticReason::known_exception)); }
        return std::make_shared<const SessionDiagnostic>(SessionDiagnostic{CommandCode::execution_error,
            DiagnosticReason::known_exception, std::move(text), std::move(context)});
    } catch (const std::overflow_error& error) {
        auto text = bounded_message(error.what());
        if (text.empty()) { text = std::string(message(DiagnosticReason::known_exception)); }
        return std::make_shared<const SessionDiagnostic>(SessionDiagnostic{CommandCode::execution_error,
            DiagnosticReason::known_exception, std::move(text), std::move(context)});
    } catch (...) {
        // No unknown what(), RTTI, stack, addresses or system paths are exposed.
    }
    return make_diagnostic(CommandCode::execution_error, DiagnosticReason::unknown_exception, std::move(context));
}
}
