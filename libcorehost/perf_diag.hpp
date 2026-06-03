#pragma once

#ifdef COREHOST_PERF_DIAG

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace conpty::perf_diag
{

enum class counter_id : size_t
{
    write_console_payload,
    write_console_convert,
    write_console_position,
    write_console_parser,
    write_console_consume_msg,
    write_console_flush_parser,
    write_console_sync_cursor,
    vt_msg_send,
    vt_msg_send_text,
    vt_raw_passthrough,
    vt_output_flush,
    vt_output_write_file,
    io_read_io_try,
    io_complete_io,
    io_server_wait_0,
    io_server_idle_wait,
    vt_input_peek,
    vt_input_read_file,
    apply_text_state,
    apply_line_feed,
    screen_set_u32,
    screen_scroll,
    row_write_glyph,
    row_write_measured_run,
    row_write_filled,
    row_write_single_layout,
    row_write_matching_layout,
    row_write_full_row,
    row_write_generic,
    count,
};

struct counter
{
    const char *name = "";
    std::atomic<unsigned long long> calls = 0;
    std::atomic<unsigned long long> ticks = 0;
    std::atomic<unsigned long long> amount = 0;
};

inline std::array<counter, static_cast<size_t>(counter_id::count)> counters = {{
    {"write_console_payload"},
    {"write_console_convert"},
    {"write_console_position"},
    {"write_console_parser"},
    {"write_console_consume_msg"},
    {"write_console_flush_parser"},
    {"write_console_sync_cursor"},
    {"vt_msg_send"},
    {"vt_msg_send_text"},
    {"vt_raw_passthrough"},
    {"vt_output_flush"},
    {"vt_output_write_file"},
    {"io_read_io_try"},
    {"io_complete_io"},
    {"io_server_wait_0"},
    {"io_server_idle_wait"},
    {"vt_input_peek"},
    {"vt_input_read_file"},
    {"apply_text_state"},
    {"apply_line_feed"},
    {"screen_set_u32"},
    {"screen_scroll"},
    {"row_write_glyph"},
    {"row_write_measured_run"},
    {"row_write_filled"},
    {"row_write_single_layout"},
    {"row_write_matching_layout"},
    {"row_write_full_row"},
    {"row_write_generic"},
}};

inline long long qpc_now() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return value.QuadPart;
}

inline long long qpc_frequency() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart;
}

inline void add(counter_id id, unsigned long long ticks, unsigned long long amount = 0) noexcept
{
    auto &c = counters[static_cast<size_t>(id)];
    c.calls.fetch_add(1, std::memory_order_relaxed);
    c.ticks.fetch_add(ticks, std::memory_order_relaxed);
    c.amount.fetch_add(amount, std::memory_order_relaxed);
}

class scope
{
  public:
    explicit scope(counter_id id, unsigned long long amount = 0) noexcept : _id(id), _amount(amount), _start(qpc_now())
    {
    }

    ~scope() noexcept
    {
        add(_id, static_cast<unsigned long long>(qpc_now() - _start), _amount);
    }

  private:
    counter_id _id;
    unsigned long long _amount;
    long long _start;
};

inline void write_summary() noexcept
{
    wchar_t temp[MAX_PATH]{};
    auto length = ::GetTempPathW(static_cast<DWORD>(sizeof(temp) / sizeof(temp[0])), temp);
    if (length == 0 || length >= sizeof(temp) / sizeof(temp[0]))
        return;

    std::wstring path{temp, length};
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');

    wchar_t filename[96]{};
    std::swprintf(filename, std::size(filename), L"corehost-perf-%lu.txt", ::GetCurrentProcessId());
    path += filename;

    auto file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    const auto freq = qpc_frequency();
    char line[384]{};
    DWORD written = 0;
    auto emit = [&](const char *text) noexcept {
        ::WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    };

    std::snprintf(line, sizeof(line), "freq=%lld ticks/sec\nname,calls,total_ms,avg_us,amount\n", freq);
    emit(line);

    for (auto &c : counters)
    {
        const auto calls = c.calls.load(std::memory_order_relaxed);
        const auto ticks = c.ticks.load(std::memory_order_relaxed);
        const auto amount = c.amount.load(std::memory_order_relaxed);
        if (calls == 0)
            continue;

        const auto total_ms = static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq);
        const auto avg_us =
            static_cast<double>(ticks) * 1000000.0 / static_cast<double>(freq) / static_cast<double>(calls);
        std::snprintf(line, sizeof(line), "%s,%llu,%.3f,%.3f,%llu\n", c.name, calls, total_ms, avg_us, amount);
        emit(line);
    }

    ::CloseHandle(file);
}

inline void ensure_registered() noexcept
{
    static const int registered = [] {
        std::atexit(write_summary);
        return 1;
    }();
    (void)registered;
}

} // namespace conpty::perf_diag

#define COREHOST_PERF_TOKEN2(prefix, line) prefix##line
#define COREHOST_PERF_TOKEN(prefix, line) COREHOST_PERF_TOKEN2(prefix, line)

#define COREHOST_PERF_SCOPE(name)                                                                                      \
    ::conpty::perf_diag::ensure_registered();                                                                          \
    ::conpty::perf_diag::scope COREHOST_PERF_TOKEN(perf_scope_, __LINE__)                                              \
    {                                                                                                                  \
        ::conpty::perf_diag::counter_id::name                                                                          \
    }

#define COREHOST_PERF_SCOPE_AMOUNT(name, amount_value)                                                                 \
    ::conpty::perf_diag::ensure_registered();                                                                          \
    ::conpty::perf_diag::scope COREHOST_PERF_TOKEN(perf_scope_, __LINE__)                                              \
    {                                                                                                                  \
        ::conpty::perf_diag::counter_id::name, static_cast<unsigned long long>(amount_value)                           \
    }

#else

#define COREHOST_PERF_SCOPE(name) ((void)0)
#define COREHOST_PERF_SCOPE_AMOUNT(name, amount_value) ((void)0)

#endif
