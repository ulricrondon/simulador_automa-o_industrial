#pragma once
// Internal translation boundary. Exception objects never cross the public session API.
#include "sim/runtime/interactive_session.hpp"
#include <exception>
namespace sim::runtime::detail {
inline constexpr std::size_t diagnostic_message_limit = 512;
DiagnosticPtr make_diagnostic(CommandCode code, DiagnosticReason reason, DiagnosticContext context);
DiagnosticPtr exception_diagnostic(std::exception_ptr exception, DiagnosticContext context);
class DiagnosticStore {
public:
    void store(DiagnosticPtr next) {
        std::lock_guard lock(mutex_); value_.swap(next);
    }
    DiagnosticPtr load() const { std::lock_guard lock(mutex_); return value_; }
private:
    mutable std::mutex mutex_;
    DiagnosticPtr value_;
};
}
