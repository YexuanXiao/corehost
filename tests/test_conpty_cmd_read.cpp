// Regression: cmd's cooked ReadConsole path must not process one Win32Input
// batch twice. Before the fix, on_input_bytes() inserted 'h' into _cooked_buf
// and the following pump_pending() replayed _readbuf[0.._read_total), so cmd
// displayed "hhh" for one key.
#include "test_common.hpp"
#include "pipe_bridge_testable.hpp"
#include "console_state.hpp"
#include "api_handlers.hpp"

#include <span>
#include <vector>

using namespace corehost::conpty;

namespace
{

struct test_context
{
    console_state state;
    screen_buffer screen;
    input_buffer input;
    pipe_bridge_testable bridge;

    test_context() : bridge(input, state, screen)
    {
    }
};

std::vector<char8_t> win32_input(char vk, char sc, wchar_t uc)
{
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "\x1b[%u;%u;%u;1;0;1_",
                                static_cast<unsigned>(vk), static_cast<unsigned>(sc),
                                static_cast<unsigned>(uc));
    return std::vector<char8_t>{reinterpret_cast<const char8_t *>(buf),
                                reinterpret_cast<const char8_t *>(buf) + n};
}

} // namespace

int main()
{
    test_context ctx;
    auto &bridge = ctx.bridge;
    bridge.test_enter_console_read_mode(13);

    const auto key = win32_input(0x48, 35, L'h');
    bridge.on_input_bytes(std::as_bytes(std::span{key.data(), key.size()}));

    if (bridge.test_get_cooked_buf() != U"h")
    {
        std::wcerr << L"after on_input_bytes: expected h, got length " << bridge.test_get_cooked_buf().size() << std::endl;
        return 1;
    }
    if (bridge.test_get_cooked_cursor() != 1)
    {
        std::wcerr << L"after on_input_bytes: cursor=" << bridge.test_get_cooked_cursor() << std::endl;
        return 1;
    }

    // message_router calls pump_pending() after processing the input batch.
    // It must not replay the same bytes from _readbuf.
    bridge.pump_pending();

    if (bridge.test_get_cooked_buf() != U"h")
    {
        std::wcerr << L"after pump_pending: expected h, got length " << bridge.test_get_cooked_buf().size() << std::endl;
        return 1;
    }
    if (bridge.test_get_cooked_cursor() != 1)
    {
        std::wcerr << L"after pump_pending: cursor=" << bridge.test_get_cooked_cursor() << std::endl;
        return 1;
    }

    std::wcout << L"cmd ReadConsole batch regression: PASSED" << std::endl;
    return 0;
}
