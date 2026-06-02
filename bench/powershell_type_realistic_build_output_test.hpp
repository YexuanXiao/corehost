#pragma once

// Parent-side runner for the realistic PowerShell `type` workload. The worker
// directory contains a generated build-output file; PowerShell reads it through
// Get-Content so this scenario exercises the shell's line/object output path.

#include "conpty_session.hpp"

namespace bench
{

inline constexpr std::string_view powershell_type_marker = "__COREHOST_BENCH_POWERSHELL_TYPE_DONE__";

[[nodiscard]] inline scenario_result run_powershell_type_realistic_build_output(std::string host, std::string name,
                                                                                std::chrono::seconds timeout)
{
    const auto command =
        L"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "
        L"\"$OutputEncoding = [Console]::OutputEncoding = [Text.Encoding]::UTF8; "
        L"Get-Content -LiteralPath '.\\corehost-bench-realistic-type-vt.txt' -Encoding UTF8 -TotalCount 12000; "
        L"Write-Output '__COREHOST_BENCH_POWERSHELL_TYPE_DONE__'\"";

    const auto begin = perf_counter();
    conpty_session session{command};
    if (!session.wait_for(powershell_type_marker, timeout))
    {
        print_and_abort("wait for PowerShell type marker timed out: scenario=%s bytes_read=%zu\n", name.c_str(),
                        session.bytes_read());
    }
    const auto end = perf_counter();
    const auto output = session.bytes_read();
    session.stop();

    return scenario_result{
        .host = std::move(host),
        .name = std::move(name),
        .input_bytes = 0,
        .output_bytes = output,
        .elapsed_ms = elapsed_ms(begin, end),
    };
}

} // namespace bench
