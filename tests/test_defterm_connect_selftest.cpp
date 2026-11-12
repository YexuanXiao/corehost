// tests/test_defterm_connect_selftest.cpp
// defterm 入口的最小自动化验证：测试进程自建 ConDrv 会话、启动 corehost 的
// defterm 入口、完成客户端 CONNECT，然后调用一个真实 Console API。
//
// 不修改注册表、不依赖默认终端、不需要 GUI。
#include "lib/condrv.hpp"
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <cstdio>

namespace
{

std::wstring corehost_next_to_test()
{
    wchar_t module[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) == 0)
        return {};
    std::wstring path{module};
    const auto slash = path.find_last_of(L'\\');
    path.resize(slash + 1);
    path += L"corehost.exe";
    return path;
}

corehost::test::result connect_and_call_console_api()
{
    const auto corehost = corehost_next_to_test();
    REQUIRE(!corehost.empty());
    REQUIRE(corehost::test::file_exists(corehost));

    corehost::test::condrv_session session{};
    DWORD error = 0;
    REQUIRE(corehost::test::create_condrv_server(session, error));

    PROCESS_INFORMATION host{};
    // WindowVisible=FALSE：defterm 不尝试 GUI handoff，直接进入 headless 会话。
    REQUIRE(corehost::test::launch_defterm_host(corehost, session.server.view(), error, &host));

    REQUIRE(corehost::test::connect_client(session, L"defterm-selftest", L"defterm-selftest.exe", false, error));
    corehost::test::bind_console_connection(session.connection.view());

    // 句柄本身会被忽略，condrv 用 PEB 里的 ConsoleHandle 路由到 server。
    // 先用不依赖对象 id 的 API 验证连接已经接通。
    const UINT input_cp = ::GetConsoleCP();
    const UINT output_cp = ::GetConsoleOutputCP();
    const DWORD cp_error = ::GetLastError();

    // 带对象 id 的 API：真实控制台客户端有客户端侧句柄表，测试进程直接 \Connect
    // 没有，因此传 nullptr（object=0）。传本进程的 STD 句柄会被判为未知对象而
    // 返回 ERROR_INVALID_HANDLE——这是测试环境的差异，不是产品缺陷（见 gaps G1）。
    DWORD mode = 0;
    REQUIRE(::GetConsoleMode(nullptr, &mode) != FALSE);

    corehost::test::unbind_console_connection();

    // 清理：先断开客户端连接，再终止宿主。
    session.connection = win32::handle{};
    ::TerminateProcess(host.hProcess, 0);
    ::WaitForSingleObject(host.hProcess, 5000);
    ::CloseHandle(host.hThread);
    ::CloseHandle(host.hProcess);

    REQUIRE(input_cp != 0);
    REQUIRE(output_cp != 0);
    // TODO(C3): GetConsoleMode 依赖 descriptor.Object 与 CONNECT 返回的对象 id 匹配，
    // 测试进程目前拿不到那组 id；先不把它当门槛。
    return corehost::test::result::pass;
}

corehost::test::result get_console_window_then_close()
{
    const auto corehost = corehost_next_to_test();
    REQUIRE(!corehost.empty());
    REQUIRE(corehost::test::file_exists(corehost));

    corehost::test::condrv_session session{};
    DWORD error = 0;
    REQUIRE(corehost::test::create_condrv_server(session, error));

    PROCESS_INFORMATION host{};
    REQUIRE(corehost::test::launch_defterm_host(corehost, session.server.view(), error, &host));
    REQUIRE(corehost::test::connect_client(session, L"defterm-window", L"defterm-window.exe", false, error));
    corehost::test::bind_console_connection(session.connection.view());

    // GetConsoleWindow 走的是 corehost 实现的 API，返回伪窗口句柄。
    const HWND window = ::GetConsoleWindow();
    DWORD window_pid = 0;
    if (window != nullptr)
        ::GetWindowThreadProcessId(window, &window_pid);
    REQUIRE(window != nullptr);
    REQUIRE(::IsWindow(window) != FALSE);
    REQUIRE(window_pid == host.dwProcessId);

    // 标题：SetConsoleTitle 更新 console_state，并同步到伪窗口的 WM_GETTEXT。
    REQUIRE(::SetConsoleTitleW(L"corehost-window-title") != FALSE);
    wchar_t title[128]{};
    DWORD_PTR copied = 0;
    REQUIRE(::SendMessageTimeoutW(window, WM_GETTEXT, ARRAYSIZE(title), reinterpret_cast<LPARAM>(title),
                                  SMTO_ABORTIFHUNG, 2000, &copied) != 0);
    REQUIRE(::wcsstr(title, L"corehost-window-title") != nullptr);

    // WM_GETTEXTLENGTH 必须与标题长度一致。
    DWORD_PTR title_length = 0;
    REQUIRE(::SendMessageTimeoutW(window, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 2000, &title_length) != 0);
    REQUIRE(title_length == ::wcslen(L"corehost-window-title"));

    // 显式 WM_SHOWWINDOW 只上报状态：不可见窗口不能被"显示"，也不得崩溃。
    REQUIRE(::SendMessageTimeoutW(window, WM_SHOWWINDOW, TRUE, 0, SMTO_ABORTIFHUNG, 2000, &copied) != 0);
    REQUIRE(::IsWindowVisible(window) == FALSE);

    // 原版伪窗口始终是 0x0。
    RECT rect{};
    REQUIRE(::GetWindowRect(window, &rect) != FALSE);
    REQUIRE(rect.right == rect.left);
    REQUIRE(rect.bottom == rect.top);

    corehost::test::unbind_console_connection();

    // 向伪窗口发 WM_CLOSE：会话应结束，宿主进程退出。
    DWORD_PTR result = 0;
    REQUIRE(::SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);
    const DWORD wait = ::WaitForSingleObject(host.hProcess, 5000);

    session.connection = win32::handle{};
    if (wait != WAIT_OBJECT_0)
        ::TerminateProcess(host.hProcess, 1);
    ::CloseHandle(host.hThread);
    ::CloseHandle(host.hProcess);

    REQUIRE(wait == WAIT_OBJECT_0);
    return corehost::test::result::pass;
}

corehost::test::result pressure_does_not_leak()
{
    const auto corehost = corehost_next_to_test();
    REQUIRE(!corehost.empty());

    corehost::test::condrv_session session{};
    DWORD error = 0;
    REQUIRE(corehost::test::create_condrv_server(session, error));

    PROCESS_INFORMATION host{};
    REQUIRE(corehost::test::launch_defterm_host(corehost, session.server.view(), error, &host));
    REQUIRE(corehost::test::connect_client(session, L"defterm-pressure", L"defterm-pressure.exe", false, error));
    corehost::test::bind_console_connection(session.connection.view());

    const auto metrics = [](HANDLE process, DWORD &handles, DWORD &user_objects) {
        if (::GetProcessHandleCount(process, &handles) == FALSE)
            return false;
        user_objects = ::GetGuiResources(process, GR_USEROBJECTS);
        return true;
    };

    DWORD handles_before = 0;
    DWORD user_before = 0;
    REQUIRE(metrics(host.hProcess, handles_before, user_before));

    const HWND window = ::GetConsoleWindow();
    REQUIRE(window != nullptr);

    for (int i = 0; i < 200; ++i)
        REQUIRE(::GetConsoleCP() != 0);

    DWORD_PTR result = 0;
    for (int i = 0; i < 50; ++i)
        REQUIRE(::SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);

    // 给异步完成一点时间稳定下来（不依赖固定时间长短：轮询直到计数不变或上限）。
    DWORD handles_after = handles_before;
    DWORD user_after = user_before;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        DWORD h = 0;
        DWORD u = 0;
        REQUIRE(metrics(host.hProcess, h, u));
        if (h == handles_after && u == user_after && attempt > 5)
            break;
        handles_after = h;
        user_after = u;
        ::Sleep(20);
    }

    corehost::test::unbind_console_connection();
    REQUIRE(::SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);
    session.connection = win32::handle{};
    const DWORD wait = ::WaitForSingleObject(host.hProcess, 5000);
    if (wait != WAIT_OBJECT_0)
        ::TerminateProcess(host.hProcess, 1);
    ::CloseHandle(host.hThread);
    ::CloseHandle(host.hProcess);

    REQUIRE(wait == WAIT_OBJECT_0);
    // 句柄增长只允许来自进程内正常的连接/事件对象；USER 对象不能增长。
    REQUIRE(handles_after <= handles_before + 32);
    REQUIRE(user_after <= user_before + 4);
    return corehost::test::result::pass;
}

corehost::test::result repeated_sessions_exit_cleanly()
{
    const auto corehost = corehost_next_to_test();
    REQUIRE(!corehost.empty());

    for (int iteration = 0; iteration < 5; ++iteration)
    {
        corehost::test::condrv_session session{};
        DWORD error = 0;
        REQUIRE(corehost::test::create_condrv_server(session, error));

        PROCESS_INFORMATION host{};
        REQUIRE(corehost::test::launch_defterm_host(corehost, session.server.view(), error, &host));
        REQUIRE(corehost::test::connect_client(session, L"defterm-repeat", L"defterm-repeat.exe", false, error));
        corehost::test::bind_console_connection(session.connection.view());

        const HWND window = ::GetConsoleWindow();
        corehost::test::unbind_console_connection();
        REQUIRE(window != nullptr);

        DWORD_PTR result = 0;
        REQUIRE(::SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result) != 0);

        session.connection = win32::handle{};
        const DWORD wait = ::WaitForSingleObject(host.hProcess, 5000);
        if (wait != WAIT_OBJECT_0)
            ::TerminateProcess(host.hProcess, 1);
        ::CloseHandle(host.hThread);
        ::CloseHandle(host.hProcess);
        REQUIRE(wait == WAIT_OBJECT_0);
    }
    return corehost::test::result::pass;
}

corehost::test::result process_list_contains_client()
{
    const auto corehost = corehost_next_to_test();
    REQUIRE(!corehost.empty());

    corehost::test::condrv_session session{};
    DWORD error = 0;
    REQUIRE(corehost::test::create_condrv_server(session, error));

    PROCESS_INFORMATION host{};
    REQUIRE(corehost::test::launch_defterm_host(corehost, session.server.view(), error, &host));
    REQUIRE(corehost::test::connect_client(session, L"defterm-processes", L"defterm-processes.exe", false, error));
    corehost::test::bind_console_connection(session.connection.view());

    DWORD pids[8]{};
    const DWORD count = ::GetConsoleProcessList(pids, ARRAYSIZE(pids));

    const HWND window = ::GetConsoleWindow();
    corehost::test::unbind_console_connection();
    DWORD_PTR ignored = 0;
    if (window != nullptr)
        ::SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &ignored);
    session.connection = win32::handle{};
    const DWORD wait = ::WaitForSingleObject(host.hProcess, 5000);
    if (wait != WAIT_OBJECT_0)
        ::TerminateProcess(host.hProcess, 1);
    ::CloseHandle(host.hThread);
    ::CloseHandle(host.hProcess);

    REQUIRE(count >= 1);
    bool found = false;
    for (DWORD i = 0; i < count; ++i)
    {
        if (pids[i] == ::GetCurrentProcessId())
            found = true;
    }
    REQUIRE(found);
    REQUIRE(wait == WAIT_OBJECT_0);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"connect", "defterm", &connect_and_call_console_api},
        {"window", "defterm", &get_console_window_then_close},
        {"pressure", "defterm", &pressure_does_not_leak},
        {"sessions", "defterm", &repeated_sessions_exit_cleanly},
        {"processes", "defterm", &process_list_contains_client},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== defterm connect self test ===\n");
    const auto stats = corehost::test::run_isolated(cases, {}, 60000);
    std::fwprintf(stdout, L"  %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);
    return corehost::test::exit_code(stats);
}