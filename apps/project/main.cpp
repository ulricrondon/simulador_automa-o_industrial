#include "sim/project/document.hpp"
#include "sim/runtime/runner.hpp"
#include "sim/core/conveyor.hpp"
#include <array>
#include <iostream>
#include <string_view>

namespace {
void print_counters(const sim::core::RunResult& result) {
    for (const auto& e : result.counter_events) {
        std::cout << "COUNTER," << e.tick << ',' << e.time_us << ',' << e.coil << ','
                  << e.state.cv << ',' << name(e.state.q) << ',' << e.state.armed << ','
                  << (e.stopped ? "stop" : "scan") << '\n';
    }
}
void print_result(const sim::core::RunResult& result, bool conveyor) {
    print_counters(result);
    if (!conveyor) {
        std::cout << "tick,time_us,input_0,output_0,position_um\n";
        for (const auto& s : result.trace) {
            std::cout << s.tick << ',' << s.time_us << ',' << static_cast<int>(s.inputs[0])
                      << ',' << static_cast<int>(s.outputs[0]) << ',' << s.position_um << '\n';
        }
        return;
    }
    std::cout << "TRACE\ntick,time_us,position_um,command,motor_power,sensor_power,plc_inputs,plc_outputs,"
                 "drive_command,motor,sensor_sampled,sensor_next,velocity_um_per_s,remainder\n";
    for (const auto& s : result.trace) {
        const auto& c = *s.conveyor;
        std::cout << s.tick << ',' << s.time_us << ',' << s.position_um << ','
                  << name(c.command.state) << ',' << name(c.motor_power.state) << ',' << name(c.sensor_power.state) << ',';
        for (std::size_t i = 0; i < c.plc_inputs.size(); ++i) {
            if (i) { std::cout << '|'; } std::cout << name(c.plc_inputs[i].state);
        }
        std::cout << ',';
        for (std::size_t i = 0; i < c.plc_outputs.size(); ++i) {
            if (i) { std::cout << '|'; } std::cout << name(c.plc_outputs[i].state);
        }
        std::cout << ',' << name(c.motor_command.state) << ',' << name(c.motor_state) << ','
                  << name(c.sampled_sensor) << ',' << name(c.sensor_state) << ','
                  << c.velocity_um_per_s << ',' << c.fractional_um_millionths << '\n';
    }
    for (const auto& s : result.trace) {
        for (std::size_t coil = 0; coil < s.timers.size(); ++coil) {
            const auto& t = s.timers[coil];
            std::cout << "TIMER," << s.tick << ',' << coil << ',' << t.elapsed_us << ',' << name(t.q) << '\n';
        }
    }
    constexpr std::array phases{"external", "sample", "ladder", "drive", "mechanics", "sensor", "control"};
    constexpr std::array kinds{"command", "motor_power", "sensor_power", "plc_input", "plc_output",
                               "motor", "sensor", "piece", "paused", "resumed", "stopped"};
    std::cout << "EVENTS\ntick,time_us,phase,kind,signal,state,position_um\n";
    for (const auto& e : result.events) {
        std::cout << e.tick << ',' << e.time_us << ',' << phases.at(static_cast<std::size_t>(e.phase))
                  << ',' << kinds.at(static_cast<std::size_t>(e.kind)) << ',' << e.signal << ',';
        if (e.kind == sim::core::TransitionKind::piece) { std::cout << ',' << e.position_um; }
        else if (e.kind == sim::core::TransitionKind::paused || e.kind == sim::core::TransitionKind::resumed ||
                 e.kind == sim::core::TransitionKind::stopped) { std::cout << ','; }
        else { std::cout << name(e.state) << ','; }
        std::cout << '\n';
    }
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && (std::wstring_view(argv[1]) == L"new" || std::wstring_view(argv[1]) == L"new-conveyor")) {
            const auto doc = std::wstring_view(argv[1]) == L"new-conveyor"
                ? sim::project::new_conveyor_demo("Esteira transportadora") : sim::project::new_demo("Projeto industrial");
            std::cout << sim::project::serialize(doc);
            return 0;
        }
        if (argc == 3 && (std::wstring_view(argv[1]) == L"validate" || std::wstring_view(argv[1]) == L"run")) {
            const auto doc = sim::project::load(std::filesystem::path(argv[2]));
            if (std::wstring_view(argv[1]) == L"validate") {
                std::cout << "Valid project " << doc.project_id << (sim::core::has_comparisons(doc.simulation.program) ? "; format 1.4; model 5; ladder-compare-v1\n" : sim::core::has_counters(doc.simulation.program) ? "; format 1.3; model 4; ladder-ctu-v1\n" : doc.simulation.conveyor
                    ? (sim::core::has_timers(doc.simulation.program) ? "; format 1.2; model 3; conveyor-ton-v1\n" : "; format 1.1; model 2; conveyor-v1\n") : "; format 1.0; model 1; discrete-v1\n");
                return 0;
            }
            sim::runtime::Runner runner;
            const auto result = runner.submit(doc.simulation).get();
            print_result(*result, doc.simulation.conveyor.has_value());
            return 0;
        }
        if (argc >= 4 && std::wstring_view(argv[1]) == L"control") {
            const auto doc = sim::project::load(std::filesystem::path(argv[2]));
            std::vector<sim::core::ControlAction> actions;
            using Action = sim::core::ControlAction;
            for (int i = 3; i < argc; ++i) {
                const std::wstring_view command(argv[i]);
                if (command == L"step") { actions.push_back(Action::step); }
                else if (command == L"pause") { actions.push_back(Action::pause); }
                else if (command == L"resume") { actions.push_back(Action::resume); }
                else if (command == L"stop") { actions.push_back(Action::stop); }
                else { throw std::invalid_argument("Unknown control command"); }
            }
            sim::runtime::Runner runner;
            const auto result = runner.submit_controlled(doc.simulation, std::move(actions)).get();
            print_result(*result, true);
            const auto& status = *result->control;
            constexpr std::array states{"running", "paused", "stopped", "completed"};
            std::cout << "CONTROL state=" << states.at(static_cast<std::size_t>(status.state))
                      << " next_tick=" << status.next_tick << " position_um=" << status.position_um
                      << " motor=" << name(status.motor) << '\n';
            return 0;
        }
        std::cerr << "Usage: sim_project new | new-conveyor | validate <json> | run <json> | "
                     "control <json> step|pause|resume|stop ...\n";
        return 2;
    } catch (const sim::project::Error& error) {
        std::cerr << "Project error [" << static_cast<int>(error.code()) << "]: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
