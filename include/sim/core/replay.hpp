#pragma once
#include "sim/core/simulation.hpp"
#include <string>

namespace sim::core {
struct TraceValue {
    std::string entity;
    std::string variable;
    std::string value;
    bool operator==(const TraceValue&) const = default;
};
struct ReplayDifference {
    Tick tick{};
    std::string entity;
    std::string variable;
    std::string expected;
    std::string actual;
};
// Read-only diagnostic projections, never used to advance simulation.
std::vector<TraceValue> snapshot_values(const Snapshot& snapshot);
std::vector<TraceValue> comparison_values(const Program& program, const std::vector<CounterState>& previous);
// Full stored state/event comparison. At most one diagnostic is retained.
std::optional<ReplayDifference> compare_runs(const RunResult& expected, const RunResult& actual);
}
