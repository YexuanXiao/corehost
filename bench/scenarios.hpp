#pragma once

// Scenario dispatcher. Individual benchmark tests live in separate headers; this
// file only runs them and emits RESULT rows for the parent process.

#include "cjk_terminal_input_test.hpp"
#include "large_mixed_vt_cjk_output_test.hpp"
#include "sgr_sequence_matrix_test.hpp"

namespace bench
{

// Emits one machine-readable RESULT row. The parent process parses only these
// tab-separated rows and ignores any diagnostic text.
inline void print_worker_result(const scenario_result &scenario)
{
    std::printf("RESULT\t%s\t%s\t%zu\t%zu\t%.6f\n", scenario.host.c_str(), scenario.name.c_str(), scenario.input_bytes,
                scenario.output_bytes, scenario.elapsed_ms);
}

// Prints the scenario result immediately for progress visibility, then stores it
// for the worker return value.
inline void append_worker_result(std::vector<scenario_result> &results, scenario_result scenario)
{
    print_worker_result(scenario);
    std::fflush(stdout);
    results.push_back(std::move(scenario));
}

inline bool should_run_scenario(std::string_view filter, std::string_view name) noexcept
{
    return filter.empty() || filter == name;
}

inline const sgr_sequence_case *filtered_sgr_case(std::string_view filter) noexcept
{
    if (!filter.starts_with("sgr-"))
        return nullptr;
    return find_sgr_sequence_case(filter.substr(4));
}

// Runs all scenarios for one host copy. This is called only in --worker mode
// from an isolated temporary directory containing the host executable named
// corehost.exe.
inline std::vector<scenario_result> run_worker_bench(std::string host_label, std::string_view scenario_filter = {})
{
    std::vector<scenario_result> results;

    if (should_run_scenario(scenario_filter, "large-mixed-vt-cjk-output"))
    {
        append_worker_result(results,
                             run_large_mixed_vt_cjk_output(host_label, "large-mixed-vt-cjk-output",
                                                           48ull * 1024ull * 1024ull, std::chrono::seconds{15}));
    }

    if (const auto *test_case = filtered_sgr_case(scenario_filter))
    {
        std::string name = "sgr-";
        name.append(test_case->name);
        append_worker_result(results, run_sgr_sequence_case(host_label, std::move(name), test_case->name,
                                                            16ull * 1024ull * 1024ull, std::chrono::seconds{15}));
    }
    else if (should_run_scenario(scenario_filter, "sgr-sequence-matrix"))
    {
        for (const auto &test_case : sgr_sequence_cases)
        {
            std::string name = "sgr-";
            name.append(test_case.name);
            append_worker_result(results, run_sgr_sequence_case(host_label, std::move(name), test_case.name,
                                                                16ull * 1024ull * 1024ull, std::chrono::seconds{15}));
        }
    }

    if (should_run_scenario(scenario_filter, "cjk-terminal-input"))
    {
        append_worker_result(results, run_cjk_terminal_input(host_label, "cjk-terminal-input", 64ull * 1024ull,
                                                             std::chrono::seconds{30}));
    }
    return results;
}

} // namespace bench
