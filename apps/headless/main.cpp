#include "sim/core/simulation.hpp"
#include "sim/runtime/runner.hpp"
#include <exception>
#include <iostream>

int main() {
    try {
        sim::runtime::Runner runner;
        const auto result = runner.submit(sim::core::demo_scenario()).get();
        std::cout << "tick,time_us,input_0,output_0,position_um\n";
        for (const auto& s : result->trace) {
            std::cout << s.tick << ',' << s.time_us << ',' << static_cast<int>(s.inputs[0])
                      << ',' << static_cast<int>(s.outputs[0]) << ',' << s.position_um << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
