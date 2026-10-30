#include "sim/core/replay.hpp"
#include <algorithm>
#include <type_traits>

namespace sim::core {
namespace {
using Values = std::vector<TraceValue>;
std::string value(DigitalState v) {
    return valid(v) ? std::string(name(v)) : "invalid(" + std::to_string(static_cast<int>(v)) + ")";
}
std::string value(bool v) { return v ? "true" : "false"; }
template<class T> std::string value(T v) {
    if constexpr (std::is_enum_v<T>) { return std::to_string(static_cast<int>(v)); }
    else { return std::to_string(v); }
}
template<class T> void put(Values& values, const std::string& entity, const std::string& variable, T v) {
    values.push_back({entity, variable, value(v)});
}
std::string indexed(const std::string& base, std::size_t index) { return base + "[" + std::to_string(index) + "]"; }
void counter(Values& v, const std::string& entity, const CounterState& c) {
    put(v,entity,"cv",c.cv);put(v,entity,"q",c.q);put(v,entity,"armed",c.armed);
}
Values event_values(const Transition& e) {
    Values v;std::string entity;
    switch(e.kind) {
    case TransitionKind::command: entity="command"; break;
    case TransitionKind::motor_power: case TransitionKind::motor: entity="motor"; break;
    case TransitionKind::sensor_power: case TransitionKind::sensor: entity="sensor"; break;
    case TransitionKind::plc_input: entity=indexed("plc.input",e.signal); break;
    case TransitionKind::plc_output: entity=indexed("plc.output",e.signal); break;
    case TransitionKind::piece: entity="axis"; break;
    default: entity="control"; break;
    }
    put(v,entity,"tick",e.tick);put(v,entity,"time_us",e.time_us);
    put(v,entity,"phase",e.phase);put(v,entity,"kind",e.kind);put(v,entity,"signal",e.signal);
    put(v,entity,"state",e.state);put(v,entity,"position_um",e.position_um);
    return v;
}
Values event_values(const CounterEvent& e) {
    Values v;const auto entity=indexed("ctu",e.coil);
    put(v,entity,"tick",e.tick);put(v,entity,"time_us",e.time_us);put(v,entity,"coil",e.coil);
    counter(v,entity,e.state);put(v,entity,"stopped",e.stopped);return v;
}
Values control_values(const std::optional<SessionStatus>& status) {
    Values v;put(v,"control","present",status.has_value());
    if(status) {
        put(v,"control","state",status->state);put(v,"control","next_tick",status->next_tick);
        put(v,"control","position_um",status->position_um);put(v,"control","motor",status->motor);
    }
    return v;
}
std::optional<ReplayDifference> compare_values(Tick tick, const Values& a, const Values& b,
                                              const std::string& prefix = {}) {
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
        if(a[i]!=b[i]) {
            return ReplayDifference{tick,a[i].entity,prefix+a[i].variable,a[i].value,b[i].value};
        }
    }
    if(a.size()!=b.size()) {
        return ReplayDifference{tick,"result",prefix+"field_count",value(a.size()),value(b.size())};
    }
    return std::nullopt;
}
void earliest(std::optional<ReplayDifference>& best, std::optional<ReplayDifference> candidate) {
    if(candidate && (!best || candidate->tick < best->tick)) { best=std::move(candidate); }
}
template<class T, class Project>
std::optional<ReplayDifference> compare_sequence(const std::vector<T>& a, const std::vector<T>& b,
                                                const std::string& label, Project project) {
    const auto common=std::min(a.size(),b.size());
    for(std::size_t i=0;i<common;++i) {
        if(a[i]==b[i]) { continue; }
        if(auto d=compare_values(std::min(a[i].tick,b[i].tick),project(a[i]),project(b[i]),
                                 indexed(label,i)+".")) { return d; }
        // Protect against new state fields omitted from a future diagnostic projection.
        return ReplayDifference{a[i].tick,label,"unmapped_state","equal","different"};
    }
    if(a.size()!=b.size()) {
        const auto tick=common<a.size()?a[common].tick:b[common].tick;
        return ReplayDifference{tick,label,"length",value(a.size()),value(b.size())};
    }
    return std::nullopt;
}
}
std::vector<TraceValue> snapshot_values(const Snapshot& s) {
    Values v;
    put(v,"scheduler","tick",s.tick);put(v,"scheduler","time_us",s.time_us);
    put(v,"axis","position_um",s.position_um);
    put(v,"plc","input_count",s.inputs.size());put(v,"plc","output_count",s.outputs.size());
    for(std::size_t i=0;i<s.inputs.size();++i) put(v,indexed("plc.input",i),"state",static_cast<DigitalState>(s.inputs[i]));
    for(std::size_t i=0;i<s.outputs.size();++i) put(v,indexed("plc.output",i),"state",static_cast<DigitalState>(s.outputs[i]));
    put(v,"timers","count",s.timers.size());
    for(std::size_t i=0;i<s.timers.size();++i) {
        const auto id=indexed("ton",i);const auto& t=s.timers[i];
        put(v,id,"elapsed_us",t.elapsed_us);put(v,id,"active",t.active);put(v,id,"q",t.q);
    }
    put(v,"counters","count",s.counters.size());
    for(std::size_t i=0;i<s.counters.size();++i) counter(v,indexed("ctu",i),s.counters[i]);
    put(v,"conveyor","present",s.conveyor.has_value());
    if(s.conveyor) {
        const auto& c=*s.conveyor;
        put(v,"command","state",c.command.state);put(v,"motor","power",c.motor_power.state);
        put(v,"sensor","power",c.sensor_power.state);
        put(v,"conveyor.plc","input_count",c.plc_inputs.size());
        put(v,"conveyor.plc","output_count",c.plc_outputs.size());
        for(std::size_t i=0;i<c.plc_inputs.size();++i) put(v,indexed("conveyor.plc.input",i),"state",c.plc_inputs[i].state);
        for(std::size_t i=0;i<c.plc_outputs.size();++i) put(v,indexed("conveyor.plc.output",i),"state",c.plc_outputs[i].state);
        put(v,"motor","command",c.motor_command.state);put(v,"motor","state",c.motor_state);
        put(v,"sensor","sampled",c.sampled_sensor);put(v,"sensor","next",c.sensor_state);
        put(v,"conveyor","velocity_um_per_s",c.velocity_um_per_s);
        put(v,"conveyor","fractional_um_millionths",c.fractional_um_millionths);
    }
    return v;
}
std::vector<TraceValue> comparison_values(const Program& program, const std::vector<CounterState>& previous) {
    validate_comparisons(program);
    Values v;
    for(const auto& rung:program.rungs) {
        for(std::size_t b=0;b<rung.branches.size();++b) {
            for(std::size_t i=0;i<rung.branches[b].size();++i) {
                const auto& comparison=rung.branches[b][i].comparison;
                if(!comparison) continue;
                const auto& c=*comparison;const auto& state=previous.at(c.counter.coil);
                const auto entity=indexed("rung",rung.coil)+"."+indexed("branch",b)+"."+indexed("condition",i);
                put(v,entity,"cv_read",state.cv);put(v,entity,"constant",c.constant);
                v.push_back({entity,"operator",std::string(comparison_name(c.op))});
                put(v,entity,"result",evaluate_comparison(state,c));
            }
        }
    }
    return v;
}
std::optional<ReplayDifference> compare_runs(const RunResult& expected, const RunResult& actual) {
    if(expected==actual) { return std::nullopt; }
    std::optional<ReplayDifference> best;
    earliest(best,compare_sequence(expected.trace,actual.trace,"trace",snapshot_values));
    earliest(best,compare_sequence(expected.events,actual.events,"event",
        [](const Transition& e){return event_values(e);}));
    earliest(best,compare_sequence(expected.counter_events,actual.counter_events,"counter_event",
        [](const CounterEvent& e){return event_values(e);}));
    const auto tick=expected.control?expected.control->next_tick:
        (actual.control?actual.control->next_tick:(expected.trace.empty()?0:expected.trace.back().tick));
    earliest(best,compare_values(tick,control_values(expected.control),control_values(actual.control)));
    if(!best) { return ReplayDifference{0,"result","unmapped_state","equal","different"}; }
    return best;
}
}
