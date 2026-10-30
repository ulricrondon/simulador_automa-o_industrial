#include "sim/core/simulation.hpp"
#include <algorithm>
namespace sim::core {
std::string_view comparison_name(ComparisonOp op) {
    switch (op) {
    case ComparisonOp::equal: return "==";
    case ComparisonOp::not_equal: return "!=";
    case ComparisonOp::less: return "<";
    case ComparisonOp::less_equal: return "<=";
    case ComparisonOp::greater: return ">";
    case ComparisonOp::greater_equal: return ">=";
    }
    throw std::invalid_argument("Invalid comparison operator");
}
DigitalState evaluate_comparison(const CounterState& counter, const CvComparison& comparison) {
    (void)comparison_name(comparison.op);
    if (counter.cv < 0 || !valid(counter.q)) { throw std::invalid_argument("Invalid counter read image"); }
    if (counter.q == DigitalState::FAULT || counter.q == DigitalState::UNDEFINED) { return counter.q; }
    bool result = false;
    // Direct int64 comparisons: no subtraction, arithmetic overflow or numeric-to-signal cast.
    switch (comparison.op) {
    case ComparisonOp::equal: result = counter.cv == comparison.constant; break;
    case ComparisonOp::not_equal: result = counter.cv != comparison.constant; break;
    case ComparisonOp::less: result = counter.cv < comparison.constant; break;
    case ComparisonOp::less_equal: result = counter.cv <= comparison.constant; break;
    case ComparisonOp::greater: result = counter.cv > comparison.constant; break;
    case ComparisonOp::greater_equal: result = counter.cv >= comparison.constant; break;
    }
    return result ? DigitalState::HIGH : DigitalState::LOW;
}
bool has_comparisons(const Program& program) {
    for (const auto& rung : program.rungs)
        for (const auto& branch : rung.branches)
            for (const auto& condition : branch)
                if (condition.comparison) { return true; }
    return false;
}
void validate_comparisons(const Program& program) {
    for (const auto& rung : program.rungs) {
        if (rung.ctu && rung.ctu->reset.comparison) {
            throw std::invalid_argument("CTU reset remains a discrete contact");
        }
        for (const auto& branch : rung.branches) {
            for (const auto& condition : branch) {
                if (!condition.comparison) { continue; }
                const auto& c = *condition.comparison;
                (void)comparison_name(c.op);
                if (condition.bank != Bank::input || condition.signal != 0 || condition.normally_closed) {
                    throw std::invalid_argument("Comparison cannot also be a discrete contact");
                }
                const auto count = std::count_if(program.rungs.begin(), program.rungs.end(),
                    [&](const auto& r) { return r.coil == c.counter.coil; });
                const auto target = std::find_if(program.rungs.begin(), program.rungs.end(),
                    [&](const auto& r) { return r.coil == c.counter.coil && r.ctu.has_value(); });
                if (c.counter.coil >= program.output_count || count != 1 || target == program.rungs.end()) {
                    throw std::invalid_argument("Missing, ambiguous or incompatible CTU reference");
                }
            }
        }
    }
}
}
