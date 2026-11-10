// tests/test_defterm_client_notify.cpp
// defterm 控制事件投递的单元测试（G16）。
//
// defterm 的 elevated / no-terminal-fallback 分支在测试环境里不可进入，
// 但"向哪个 pid 投递什么事件、走 ConsoleControl 而不是
// GenerateConsoleCtrlEvent"这一点可以注入验证。
#include "test_common.hpp"

#include "defterm/client_notify.hpp"

#include <windows.h>

namespace
{

struct captured_event
{
    DWORD pid = 0;
    DWORD event = 0;
    HWND window = nullptr;
    int calls = 0;
};

captured_event g_captured;

NTSTATUS WINAPI recording_sender(DWORD pid, DWORD event, HWND console_window) noexcept
{
    g_captured.pid = pid;
    g_captured.event = event;
    g_captured.window = console_window;
    ++g_captured.calls;
    return 0;
}

// 投递目标必须是客户端 pid、事件码 CTRL_BREAK_EVENT、窗口句柄为空。
bool ctrl_break_goes_to_client_pid()
{
    g_captured = {};
    corehost::defterm::notify_client_ctrl(4242, CTRL_BREAK_EVENT, &recording_sender);

    ASSERT_EQ(g_captured.calls, 1);
    ASSERT_EQ(g_captured.pid, 4242u);
    ASSERT_EQ(g_captured.event, static_cast<DWORD>(CTRL_BREAK_EVENT));
    ASSERT(g_captured.window == nullptr);
    return true;
}

// Ctrl+C 的事件码是 0；投递函数不能把 0 当成"没有事件"。
bool ctrl_c_event_code_zero_is_delivered()
{
    g_captured = {};
    corehost::defterm::notify_client_ctrl(7, CTRL_C_EVENT, &recording_sender);

    ASSERT_EQ(g_captured.calls, 1);
    ASSERT_EQ(g_captured.pid, 7u);
    ASSERT_EQ(g_captured.event, static_cast<DWORD>(CTRL_C_EVENT));
    return true;
}

// 失败状态只记录，不影响调用方流程（defterm 继续通知/退出）。
bool failure_status_does_not_throw()
{
    struct local
    {
        static NTSTATUS WINAPI failing_sender(DWORD, DWORD, HWND) noexcept
        {
            return static_cast<NTSTATUS>(0xC0000008L); // STATUS_INVALID_HANDLE
        }
    };

    corehost::defterm::notify_client_ctrl(1, CTRL_BREAK_EVENT, &local::failing_sender);
    return true;
}

} // namespace

int main()
{
    std::wcout << L"=== Defterm Client Notify ===" << std::endl;
    RUN_TEST(ctrl_break_goes_to_client_pid, L"Ctrl+Break goes to client pid");
    RUN_TEST(ctrl_c_event_code_zero_is_delivered, L"Ctrl+C event code 0 is delivered");
    RUN_TEST(failure_status_does_not_throw, L"Failure status does not throw");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, "
               << (tests_passed + tests_failed) << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
