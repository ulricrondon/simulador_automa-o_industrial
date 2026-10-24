#include "sim/project/document.hpp"
#include "sim/runtime/runner.hpp"
#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"new") {
            std::cout << sim::project::serialize(sim::project::new_demo("Projeto industrial"));
            return 0;
        }
        if (argc == 3 && (std::wstring_view(argv[1]) == L"validate" || std::wstring_view(argv[1]) == L"run")) {
            const auto document = sim::project::load(std::filesystem::path(argv[2]));
            if (std::wstring_view(argv[1]) == L"validate") {
                std::cout << "Valid project " << document.project_id << "; format 1.0; model 1; discrete-v1\n";
                return 0;
            }
            sim::runtime::Runner runner;
            const auto result = runner.submit(document.simulation).get();
            std::cout << "tick,time_us,input_0,output_0,position_um\n";
            for (const auto& snapshot : result->trace) {
                std::cout << snapshot.tick << ',' << snapshot.time_us << ',' << static_cast<int>(snapshot.inputs[0])
                          << ',' << static_cast<int>(snapshot.outputs[0]) << ',' << snapshot.position_um << '\n';
            }
            return 0;
        }
        std::cerr << "Usage: sim_project new | validate <project.json> | run <project.json>\n";
        return 2;
    } catch (const sim::project::Error& error) {
        std::cerr << "Project error [" << static_cast<int>(error.code()) << "]: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
