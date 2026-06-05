#pragma once

// Parent-side runner for one SGR/text-effect case. scenarios.hpp iterates the
// shared case list and emits one RESULT row per sequence.

#include "conpty_session.hpp"
#include "sgr_sequence_matrix_cases.hpp"

namespace bench
{

[[nodiscard]] inline scenario_result run_sgr_sequence_case(std::string host, std::string name,
                                                           std::string_view case_name, size_t target_bytes,
                                                           std::chrono::milliseconds timeout)
{
    constexpr std::string_view marker = "__CONPTY_BENCH_SGR_DONE__";
    constexpr std::string_view ready = "__CONPTY_BENCH_SGR_READY__";
    auto trigger_name = unique_event_name();
    win32::event trigger{win32::create_tag, true, false, trigger_name};

    auto command = quote(current_exe_path()) + L" --emit-sgr-case " + quote(widen(case_name)) + L" " +
                   std::to_wstring(target_bytes) + L" " + quote(widen(marker)) + L" " + quote(widen(ready)) + L" " +
                   quote(trigger_name);

    conpty_session session{std::move(command)};
    if (!session.wait_for(ready, std::chrono::seconds{10}))
    {
        print_and_abort("wait for SGR ready timed out: scenario=%s bytes_read=%zu\n", name.c_str(),
                        session.bytes_read());
    }

    const size_t before = session.bytes_read();
    const int64_t begin = perf_counter();
    trigger.set();
    if (!session.wait_for(marker, timeout))
    {
        print_and_abort("wait for SGR marker timed out: scenario=%s bytes_read=%zu\n", name.c_str(),
                        session.bytes_read());
    }
    const int64_t end = perf_counter();
    const size_t after = session.bytes_read();

    session.stop();
    return scenario_result{
        .host = std::move(host),
        .name = std::move(name),
        .input_bytes = 0,
        .output_bytes = after - before,
        .elapsed_ms = elapsed_ms(begin, end),
    };
}

} // namespace bench
