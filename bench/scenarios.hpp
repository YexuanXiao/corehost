#pragma once

// Scenario dispatcher. Individual benchmark tests live in separate headers; this
// file only runs them and emits RESULT rows for the parent process.

#include "cjk_terminal_input_test.hpp"
#include "large_mixed_vt_cjk_output_test.hpp"
#include "long_line_three_vt_output_test.hpp"
#include "powershell_type_realistic_build_output_test.hpp"
#include "sgr_sequence_matrix_test.hpp"

namespace bench
{

inline constexpr size_t benchmark_rounds = 1;

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

inline void append_worker_sample(std::vector<scenario_result> &results, scenario_result scenario)
{
    for (auto &result : results)
    {
        if (result.name == scenario.name)
        {
            result.input_bytes += scenario.input_bytes;
            result.output_bytes += scenario.output_bytes;
            result.elapsed_ms += scenario.elapsed_ms;
            return;
        }
    }
    results.push_back(std::move(scenario));
}

inline void average_worker_samples(std::vector<scenario_result> &results)
{
    if constexpr (benchmark_rounds == 1)
        return;

    for (auto &result : results)
    {
        result.input_bytes /= benchmark_rounds;
        result.output_bytes /= benchmark_rounds;
        result.elapsed_ms /= static_cast<double>(benchmark_rounds);
    }
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

    for (size_t round = 0; round < benchmark_rounds; ++round)
    {
        if (should_run_scenario(scenario_filter, "large-mixed-vt-cjk-output"))
        {
            append_worker_sample(results,
                                 run_large_mixed_vt_cjk_output(host_label, "large-mixed-vt-cjk-output",
                                                               24ull * 1024ull * 1024ull, std::chrono::seconds{10}));
        }

        if (should_run_scenario(scenario_filter, "long-line-three-vt-output"))
        {
            append_worker_sample(results,
                                 run_long_line_three_vt_output(host_label, "long-line-three-vt-output",
                                                               16ull * 1024ull * 1024ull, std::chrono::seconds{10}));
        }

        if (should_run_scenario(scenario_filter, "powershell-type-realistic-build-output"))
        {
            append_worker_sample(results,
                                 run_powershell_type_realistic_build_output(
                                     host_label, "powershell-type-realistic-build-output", std::chrono::seconds{30}));
        }

        if (const auto *test_case = filtered_sgr_case(scenario_filter))
        {
            std::string name = "sgr-";
            name.append(test_case->name);
            append_worker_sample(results, run_sgr_sequence_case(host_label, std::move(name), test_case->name,
                                                                4ull * 1024ull * 1024ull, std::chrono::seconds{10}));
        }
        else if (should_run_scenario(scenario_filter, "sgr-sequence-matrix"))
        {
            for (const auto &test_case : sgr_sequence_cases)
            {
                std::string name = "sgr-";
                name.append(test_case.name);
                append_worker_sample(results, run_sgr_sequence_case(host_label, std::move(name), test_case.name,
                                                                    4ull * 1024ull * 1024ull,
                                                                    std::chrono::seconds{10}));
            }
        }

        if (should_run_scenario(scenario_filter, "cjk-terminal-input"))
        {
            append_worker_sample(results, run_cjk_terminal_input(host_label, "cjk-terminal-input", 384ull * 1024ull,
                                                                 std::chrono::seconds{10}));
        }
    }

    average_worker_samples(results);
    for (const auto &result : results)
        print_worker_result(result);
    return results;
}

} // namespace bench
