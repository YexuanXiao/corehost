#pragma once

// Parent-process runner. It copies each host into an isolated temporary worker
// directory, starts a worker conpty_bench process there, parses RESULT rows, and
// prints single-host or two-host comparison tables.

#include "scenarios.hpp"
#include "cjk_terminal_input_worker.hpp"
#include "large_mixed_vt_cjk_output_worker.hpp"
#include "long_line_three_vt_output_worker.hpp"
#include "sgr_sequence_matrix_worker.hpp"

#include <array>
#include <charconv>

namespace bench
{

// Split one RESULT row. RESULT rows are tab-separated to avoid ambiguity with
// scenario labels or host filenames.
[[nodiscard]] inline std::vector<std::string_view> split_tabs(std::string_view line)
{
    std::vector<std::string_view> parts;
    while (!line.empty())
    {
        const size_t pos = line.find('\t');
        parts.push_back(line.substr(0, pos));
        if (pos == std::string_view::npos)
            break;
        line.remove_prefix(pos + 1);
    }
    return parts;
}

inline size_t parse_size_field(std::string_view value, const char *field_name)
{
    size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    {
        print_and_abort("invalid RESULT %s: %.*s\n", field_name, static_cast<int>(value.size()), value.data());
    }
    return result;
}

inline double parse_double_field(std::string_view value, const char *field_name)
{
    double result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    {
        print_and_abort("invalid RESULT %s: %.*s\n", field_name, static_cast<int>(value.size()), value.data());
    }
    return result;
}

// Extract scenario_result values from worker stdout. Non-RESULT text is ignored
// so diagnostics can still be printed without breaking parsing.
[[nodiscard]] inline std::vector<scenario_result> parse_worker_results(std::string_view output)
{
    std::vector<scenario_result> results;
    while (!output.empty())
    {
        size_t end = output.find('\n');
        auto line = output.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        const auto parts = split_tabs(line);
        if (parts.size() == 6 && parts[0] == "RESULT")
        {
            results.push_back(scenario_result{
                .host = std::string{parts[1]},
                .name = std::string{parts[2]},
                .input_bytes = parse_size_field(parts[3], "input_bytes"),
                .output_bytes = parse_size_field(parts[4], "output_bytes"),
                .elapsed_ms = parse_double_field(parts[5], "elapsed_ms"),
            });
        }

        if (end == std::string_view::npos)
            break;
        output.remove_prefix(end + 1);
    }
    return results;
}

// Temporary per-host working directory. libconpty discovers the host by looking
// for corehost.exe next to the worker executable, so each host needs isolation.
[[nodiscard]] inline std::filesystem::path make_worker_dir(size_t index)
{
    std::wstring temp;
    temp.resize(MAX_PATH);
    const DWORD length = ::GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length > temp.size())
    {
        print_and_abort("GetTempPathW failed: %lu\n", ::GetLastError());
    }
    temp.resize(length);

    auto dir = std::filesystem::path{temp} /
               (L"corehost-conpty-bench-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(index));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        print_and_abort("create worker directory failed: %d\n", ec.value());
    }
    return dir;
}

// Copy the selected host as corehost.exe plus sibling DLLs it may need.
inline void copy_host_tree(const std::filesystem::path &host_path, const std::filesystem::path &worker_dir)
{
    std::error_code ec;
    std::filesystem::copy_file(host_path, worker_dir / L"corehost.exe",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        print_and_abort("copy host executable failed: %d\n", ec.value());
    }

    const auto source_dir = host_path.parent_path();
    if (!source_dir.empty())
    {
        for (std::filesystem::directory_iterator it{source_dir, ec}, end; !ec && it != end; it.increment(ec))
        {
            const auto &entry = *it;
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() == L".dll")
            {
                std::filesystem::copy_file(entry.path(), worker_dir / entry.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    print_and_abort("copy host dll failed: %d\n", ec.value());
                }
            }
        }
        if (ec)
        {
            print_and_abort("scan host directory failed: %d\n", ec.value());
        }
    }
}

inline constexpr wchar_t powershell_type_input_file_name[] = L"corehost-bench-realistic-type-vt.txt";

inline void generate_powershell_type_input_file(const std::filesystem::path &worker_dir)
{
    const auto path = worker_dir / powershell_type_input_file_name;
    FILE *file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
        print_and_abort("create PowerShell type input file failed\n");

    constexpr size_t line_count = 12000;
    for (size_t i = 0; i < line_count; ++i)
    {
        const char *prefix = "\x1b[32m[build]\x1b[0m ";
        if (i % 17 == 0)
            prefix = "\x1b[33mwarning\x1b[0m ";
        else if (i % 53 == 0)
            prefix = "\x1b[31merror\x1b[0m ";
        else if (i % 7 == 0)
            prefix = "\x1b[36mcompile\x1b[0m ";

        if (std::fprintf(file,
                         "%sproject\\module\\target_%05zu.cpp : ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
                         "\x1b[1m喜欢你 核心终端性能测试\x1b[22m payload payload payload payload\r\n",
                         prefix, i) < 0)
        {
            std::fclose(file);
            print_and_abort("write PowerShell type input file failed\n");
        }
    }

    if (std::fclose(file) != 0)
        print_and_abort("close PowerShell type input file failed\n");
}

// Launches the worker process and mirrors its stdout/stderr to this process.
// The returned string is parsed after the worker exits.
[[nodiscard]] inline std::string run_worker_process(const std::filesystem::path &worker_exe,
                                                    const std::filesystem::path &worker_dir, std::string_view label,
                                                    std::string_view scenario_filter)
{
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    win32::handle read_pipe;
    win32::handle write_pipe;
    if (!::CreatePipe(read_pipe.put(), write_pipe.put(), &inheritable, 0))
    {
        print_and_abort("CreatePipe(worker stdout) failed: %lu\n", ::GetLastError());
    }
    if (!::SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0))
    {
        print_and_abort("SetHandleInformation(worker stdout) failed: %lu\n", ::GetLastError());
    }

    const auto wide_label = widen(label);
    auto command = quote(worker_exe.wstring()) + L" --worker " + quote(wide_label);
    if (!scenario_filter.empty())
        command += L" " + quote(widen(scenario_filter));

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe.get();
    startup.hStdError = write_pipe.get();

    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                          worker_dir.c_str(), &startup, &pi))
    {
        print_and_abort("CreateProcessW(worker) failed: %lu\n", ::GetLastError());
    }

    win32::handle process{pi.hProcess};
    ::CloseHandle(pi.hThread);
    write_pipe.clear();

    std::string output;
    char buffer[64 * 1024];
    for (;;)
    {
        DWORD read = 0;
        if (!::ReadFile(read_pipe.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0)
            break;
        output.append(buffer, read);
        std::fwrite(buffer, 1, read, stdout);
        std::fflush(stdout);
    }

    ::WaitForSingleObject(process.get(), INFINITE);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.get(), &exit_code);
    if (exit_code != 0)
    {
        std::fwrite(output.data(), 1, output.size(), stderr);
        print_and_abort("worker process failed: %lu\n", exit_code);
    }
    return output;
}

// Run all scenarios for one host executable by copying the bench and host into a
// temporary directory, then invoking conpty_bench --worker <label>.
[[nodiscard]] inline host_result run_host_via_worker(const wchar_t *host_path, std::string label, size_t index,
                                                     std::string_view scenario_filter)
{
    const auto worker_dir = make_worker_dir(index);
    const auto worker_exe = worker_dir / L"conpty_bench_worker.exe";

    std::filesystem::copy_file(current_exe_path(), worker_exe, std::filesystem::copy_options::overwrite_existing);
    copy_host_tree(host_path, worker_dir);
    generate_powershell_type_input_file(worker_dir);

    auto output = run_worker_process(worker_exe, worker_dir, label, scenario_filter);
    auto scenarios = parse_worker_results(output);
    if (scenarios.empty())
    {
        std::fwrite(output.data(), 1, output.size(), stderr);
        print_and_abort("parse worker results failed: %lu\n", ERROR_INVALID_DATA);
    }

    return host_result{
        .label = std::move(label),
        .scenarios = std::move(scenarios),
    };
}

// Human-readable scenario table for one host.
inline void print_result(const host_result &host)
{
    std::printf("\n# %s\n", host.label.c_str());
    std::printf("%-22s %12s %12s %12s %14s %14s\n", "scenario", "elapsed(ms)", "input(bytes)", "output(bytes)",
                "input(MiB/s)", "output(MiB/s)");

    for (const auto &scenario : host.scenarios)
    {
        const double seconds = scenario.elapsed_ms / 1000.0;
        const double input_mib = static_cast<double>(scenario.input_bytes) / (1024.0 * 1024.0);
        const double output_mib = static_cast<double>(scenario.output_bytes) / (1024.0 * 1024.0);
        std::printf("%-22s %12.3f %12zu %12zu %14.2f %14.2f\n", scenario.name.c_str(), scenario.elapsed_ms,
                    scenario.input_bytes, scenario.output_bytes, input_mib / seconds, output_mib / seconds);
    }
}

// Pairwise comparison table. Scenarios are matched by order because both workers
// run the same fixed scenario list.
inline void print_comparison(const host_result &baseline, const host_result &candidate)
{
    std::printf("\n# comparison: %s / %s\n", candidate.label.c_str(), baseline.label.c_str());
    std::printf("%-22s %14s %18s\n", "scenario", "time ratio", "throughput ratio");
    for (size_t i = 0; i < std::min(baseline.scenarios.size(), candidate.scenarios.size()); ++i)
    {
        const auto &base = baseline.scenarios[i];
        const auto &cand = candidate.scenarios[i];
        const double time_ratio = cand.elapsed_ms / base.elapsed_ms;
        const double base_bytes = static_cast<double>(base.input_bytes + base.output_bytes);
        const double cand_bytes = static_cast<double>(cand.input_bytes + cand.output_bytes);
        const double base_throughput = base_bytes / base.elapsed_ms;
        const double cand_throughput = cand_bytes / cand.elapsed_ms;
        std::printf("%-22s %14.3f %18.3f\n", cand.name.c_str(), time_ratio, cand_throughput / base_throughput);
    }
}

// Top-level command dispatcher for normal mode and hidden worker modes.
inline int run(int argc, wchar_t **argv)
{
    if ((argc >= 3 && argc <= 6) && wcscmp(argv[1], L"--emit-mixed") == 0)
        return emit_mixed_vt_cjk_to_stdout(static_cast<size_t>(_wcstoui64(argv[2], nullptr, 10)),
                                           argc >= 4 ? argv[3] : L"", argc >= 5 ? argv[4] : L"",
                                           argc == 6 ? argv[5] : L"");

    if ((argc >= 3 && argc <= 6) && wcscmp(argv[1], L"--emit-long-line-3vt") == 0)
        return emit_long_line_three_vt_to_stdout(static_cast<size_t>(_wcstoui64(argv[2], nullptr, 10)),
                                                 argc >= 4 ? argv[3] : L"", argc >= 5 ? argv[4] : L"",
                                                 argc == 6 ? argv[5] : L"");

    if ((argc >= 4 && argc <= 7) && wcscmp(argv[1], L"--emit-sgr-case") == 0)
        return emit_sgr_sequence_case_to_stdout(argv[2], static_cast<size_t>(_wcstoui64(argv[3], nullptr, 10)),
                                                argc >= 5 ? argv[4] : L"", argc >= 6 ? argv[5] : L"",
                                                argc == 7 ? argv[6] : L"");

    if ((argc == 3 || argc == 4) && wcscmp(argv[1], L"--consume-input") == 0)
        return consume_stdin_until_marker(argv[2], argc == 4 ? argv[3] : L"");

    if ((argc == 3 || argc == 4) && wcscmp(argv[1], L"--worker") == 0)
    {
        run_worker_bench(narrow(argv[2]), argc == 4 ? narrow(argv[3]) : std::string{});
        return 0;
    }

    std::string scenario_filter;
    int host_arg = 1;
    if (argc >= 4 && wcscmp(argv[1], L"--scenario") == 0)
    {
        scenario_filter = narrow(argv[2]);
        host_arg = 3;
    }

    const int host_count = argc - host_arg;
    if (host_count != 1 && host_count != 2)
    {
        std::printf("Usage:\n");
        std::printf("  conpty_bench.exe <corehost.exe>\n");
        std::printf("  conpty_bench.exe <OpenConsole.exe> <corehost.exe>\n");
        std::printf("  conpty_bench.exe --scenario <name> <corehost.exe>\n");
        std::printf("  conpty_bench.exe --scenario <name> <OpenConsole.exe> <corehost.exe>\n");
        return 1;
    }

    validate_host_path(argv[host_arg]);
    if (host_count == 1)
    {
        auto result = run_host_via_worker(argv[host_arg], label_from_path(argv[host_arg]), 0, scenario_filter);
        print_result(result);
        return 0;
    }

    validate_host_path(argv[host_arg + 1]);
    auto baseline = run_host_via_worker(argv[host_arg], label_from_path(argv[host_arg]), 0, scenario_filter);
    auto candidate = run_host_via_worker(argv[host_arg + 1], label_from_path(argv[host_arg + 1]), 1, scenario_filter);
    print_result(baseline);
    print_result(candidate);
    print_comparison(baseline, candidate);
    return 0;
}

} // namespace bench
