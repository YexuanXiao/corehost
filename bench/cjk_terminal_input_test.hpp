#pragma once

// Measures PTY input -> host -> child stdin delivery. The payload is CR-delimited
// CJK/ASCII text so cooked-input hosts can deliver the marker reliably.

#include "conpty_session.hpp"

namespace bench
{

[[nodiscard]] inline std::string create_cjk_terminal_input_payload(size_t target_bytes, std::string_view marker)
{
    const std::string line = "input payload ASCII abcdefghijklmnopqrstuvwxyz 0123456789 "
                             "喜欢你 核心终端 输入性能 "
                             "payload payload payload payload\r";

    std::string payload;
    payload.reserve(target_bytes + line.size() + marker.size());
    while (payload.size() < target_bytes)
        payload.append(line);
    payload.append(marker);
    payload.push_back('\r');
    return payload;
}

[[nodiscard]] inline scenario_result run_cjk_terminal_input(std::string host, std::string name, size_t target_bytes,
                                                            std::chrono::milliseconds timeout)
{
    constexpr std::string_view marker = "__CONPTY_BENCH_INPUT_DONE__";
    constexpr std::string_view ready = "__CONPTY_BENCH_INPUT_READY__";
    auto command = quote(current_exe_path()) + L" --consume-input " + quote(widen(marker)) + L" " + quote(widen(ready));
    auto payload = create_cjk_terminal_input_payload(target_bytes, marker);

    conpty_session session{std::move(command)};
    if (!session.wait_for(ready, std::chrono::seconds{10}))
    {
        print_and_abort("wait for direct input ready timed out: scenario=%s bytes_read=%zu\n", name.c_str(),
                        session.bytes_read());
    }

    const size_t before = session.bytes_read();
    const int64_t begin = perf_counter();
    session.write(payload);
    if (!session.wait_for_after(marker, before, timeout))
    {
        print_and_abort("wait for direct input marker timed out: scenario=%s bytes_read=%zu\n", name.c_str(),
                        session.bytes_read());
    }
    const int64_t end = perf_counter();
    const size_t after = session.bytes_read();

    session.stop();
    return scenario_result{
        .host = std::move(host),
        .name = std::move(name),
        .input_bytes = payload.size(),
        .output_bytes = after - before,
        .elapsed_ms = elapsed_ms(begin, end),
    };
}

} // namespace bench
