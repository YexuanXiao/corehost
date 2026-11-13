// tests/test_conpty_window.cpp
// GetConsoleWindow 伪窗口的端到端验证（B-R1..B-R6）。
//
// 测试进程创建 ConPTY → corehost 在独立线程创建不可见伪窗口 → 测试通过
// EnumWindows 找到该窗口，验证：
//   - 窗口属于 corehost 进程、可被枚举、不可见；
//   - 同步消息不挂起；
//   - WM_CLOSE 在有限步内结束会话（corehost 进程退出）。
#include "test_common.hpp"
#include "conpty_test_helpers.hpp"
#include "lib/conpty_session.hpp"

#include <tlhelp32.h>

#include <cstdio>

using namespace corehost::test;

namespace
{

DWORD find_corehost_child()
{
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    const DWORD parent = ::GetCurrentProcessId();
    DWORD found = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok != FALSE; ok = ::Process32NextW(snapshot, &entry))
    {
        if (entry.th32ParentProcessID == parent && ::_wcsicmp(entry.szExeFile, L"corehost.exe") == 0)
            found = entry.th32ProcessID;
    }
    ::CloseHandle(snapshot);
    return found;
}

struct window_search
{
    DWORD process_id = 0;
    HWND found = nullptr;
};

BOOL CALLBACK enum_window_proc(HWND hwnd, LPARAM param)
{
    auto *search = reinterpret_cast<window_search *>(param);

    DWORD window_pid = 0;
    if (::GetWindowThreadProcessId(hwnd, &window_pid) == 0 || window_pid != search->process_id)
        return TRUE;

    wchar_t class_name[64]{};
    if (::GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) == 0)
        return TRUE;

    if (::wcscmp(class_name, L"PseudoConsoleWindow") == 0)
    {
        search->found = hwnd;
        return FALSE; // 找到了
    }
    return TRUE;
}

bool pseudo_window_contract()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));

    DWORD pid = 0;
    for (int attempt = 0; attempt < 100 && pid == 0; ++attempt)
    {
        pid = find_corehost_child();
        if (pid == 0)
            sleep_ms(20);
    }
    ASSERT(pid != 0);

    // B-R1/R2：窗口可被枚举，属于 corehost 的专用线程。
    window_search search{pid, nullptr};
    for (int attempt = 0; attempt < 100 && search.found == nullptr; ++attempt)
    {
        ::EnumWindows(&enum_window_proc, reinterpret_cast<LPARAM>(&search));
        if (search.found == nullptr)
            sleep_ms(20);
    }
    ASSERT(search.found != nullptr);
    ASSERT(::IsWindow(search.found) != FALSE);

    DWORD window_thread = ::GetWindowThreadProcessId(search.found, nullptr);
    ASSERT(window_thread != 0);

    // B-R3：不可见。
    ASSERT(::IsWindowVisible(search.found) == FALSE);

    // B-R4：同步消息不挂起。
    DWORD_PTR result = 0;
    const auto send = ::SendMessageTimeoutW(search.found, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
    ASSERT(send != 0);

    // 原版伪窗口始终是 0x0，PtySignal::ResizeWindow 不改变它。
    RECT rect{};
    ASSERT(::GetWindowRect(search.found, &rect) != FALSE);
    ASSERT(rect.right == rect.left);
    ASSERT(rect.bottom == rect.top);

    // owner：SetParent 信号必须落到 GWLP_HWNDPARENT，并反映到 GA_ROOTOWNER。
    const HWND owner = ::GetDesktopWindow();
    ASSERT(SUCCEEDED(ConptyReparentPseudoConsole(ci.hpc, owner)));
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (reinterpret_cast<HWND>(::GetWindowLongPtrW(search.found, GWLP_HWNDPARENT)) == owner)
            break;
        sleep_ms(20);
    }
    ASSERT(reinterpret_cast<HWND>(::GetWindowLongPtrW(search.found, GWLP_HWNDPARENT)) == owner);
    ASSERT(::GetAncestor(search.found, GA_ROOTOWNER) == owner);

    // 未 CONNECT 时 show/hide 只暂存，窗口保持原状且不回显 VT。
    drain_output(ci.hOutput);
    ASSERT(SUCCEEDED(ConptyShowHidePseudoConsole(ci.hpc, FALSE)));
    sleep_ms(100);
    ASSERT(::IsIconic(search.found) == FALSE);
    ASSERT(SUCCEEDED(ConptyShowHidePseudoConsole(ci.hpc, TRUE)));
    sleep_ms(100);
    ASSERT(::IsIconic(search.found) == FALSE);

    const auto visibility_output = read_output_string(ci.hOutput, 300);
    ASSERT(visibility_output.find("\x1b[1t") == std::string::npos);
    ASSERT(visibility_output.find("\x1b[2t") == std::string::npos);

    // WM_ACTIVATE 在有 owner 时只把焦点交还 owner，不应改变前台窗口。
    const HWND foreground = ::GetForegroundWindow();
    (void)::SendMessageTimeoutW(search.found, WM_ACTIVATE, WA_ACTIVE, 0, SMTO_ABORTIFHUNG, 2000, &result);
    ASSERT(::GetForegroundWindow() == foreground);

    // B-R6：WM_CLOSE 必须及时返回，并结束会话（corehost 进程退出）。
    // 主线程阻塞在 READ_IO 上，窗口线程会 CancelSynchronousIo 打断它。
    const auto close = ::SendMessageTimeoutW(search.found, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
    ASSERT(close != 0);

    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    ASSERT(process != nullptr);
    const DWORD wait = ::WaitForSingleObject(process, 5000);
    ::CloseHandle(process);
    ASSERT(wait == WAIT_OBJECT_0);

    return true;
}

// 连接前的 show/hide 只暂存；启动客户端后会话仍存活。
bool pseudo_window_preconnect_signals()
{
    conpty_session session;
    ASSERT(session.create({80, 25}));
    ASSERT(session.reparent(::GetDesktopWindow()));
    ASSERT(session.show(true));

    DWORD pid = 0;
    for (int attempt = 0; attempt < 100 && pid == 0; ++attempt)
    {
        pid = find_corehost_child();
        if (pid == 0)
            sleep_ms(20);
    }
    ASSERT(pid != 0);

    window_search search{pid, nullptr};
    for (int attempt = 0; attempt < 100 && search.found == nullptr; ++attempt)
    {
        ::EnumWindows(&enum_window_proc, reinterpret_cast<LPARAM>(&search));
        if (search.found == nullptr)
            sleep_ms(20);
    }
    ASSERT(search.found != nullptr);
    sleep_ms(100);
    ASSERT(::IsWindowVisible(search.found) == FALSE);

    ASSERT(session.launch(L"cmd.exe /c \"echo READY & timeout /t 10 /nobreak >nul\""));
    ASSERT(session.wait_output("READY", 5000));
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (::IsWindowVisible(search.found) != FALSE)
            break;
        sleep_ms(20);
    }
    ASSERT(::IsWindowVisible(search.found) != FALSE);
    ASSERT(session.is_running());
    session.close();
    return true;
}

// 连接前信号后启动交互式 PowerShell，客户端必须保持运行。
bool interactive_pwsh_preconnect_signals()
{
    conpty_session session;
    ASSERT(session.create({120, 30}));
    ASSERT(session.reparent(::GetDesktopWindow()));
    ASSERT(session.show(true));
    ASSERT(session.show(false));
    ASSERT(session.show(true));
    ASSERT(session.resize({100, 30}));

    ASSERT(session.launch(L"pwsh.exe -NoLogo -NoProfile -Command \"Write-Output READY; Start-Sleep -Seconds 10\""));
    ASSERT(session.wait_output("READY", 10000));
    sleep_ms(1000);
    ASSERT(session.is_running());
    session.close();
    return true;
}

// 交互式 PowerShell 在收到宿主信号后不应主动退出。
bool interactive_pwsh_survives_signals()
{
    conpty_session session;
    ASSERT(session.start(L"pwsh.exe -NoLogo -NoProfile", {120, 30}));
    ASSERT(session.write("Write-Output READY\r"));
    ASSERT(session.wait_output("READY", 10000));
    ASSERT(session.write("e"));
    sleep_ms(300);
    ASSERT(session.is_running());

    ASSERT(session.reparent(::GetDesktopWindow()));
    ASSERT(session.show(true));
    ASSERT(session.resize({100, 30}));
    sleep_ms(1000);
    ASSERT(session.is_running());
    session.close();
    return true;
}

// 连接后的 show/hide 才应用到窗口；用独立客户端模拟宿主时机。
bool pseudo_window_after_connect()
{
    conpty_session session;
    ASSERT(session.start(L"cmd.exe /c \"echo READY & timeout /t 10 /nobreak >nul\"", {80, 25}));
    ASSERT(session.wait_output("READY", 5000));

    DWORD pid = 0;
    for (int attempt = 0; attempt < 100 && pid == 0; ++attempt)
    {
        pid = find_corehost_child();
        if (pid == 0)
            sleep_ms(20);
    }
    ASSERT(pid != 0);

    window_search search{pid, nullptr};
    for (int attempt = 0; attempt < 100 && search.found == nullptr; ++attempt)
    {
        ::EnumWindows(&enum_window_proc, reinterpret_cast<LPARAM>(&search));
        if (search.found == nullptr)
            sleep_ms(20);
    }
    ASSERT(search.found != nullptr);

    ASSERT(session.reparent(::GetDesktopWindow()));
    ASSERT(session.show(false));
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (::IsIconic(search.found) != FALSE)
            break;
        sleep_ms(20);
    }
    ASSERT(::IsIconic(search.found) != FALSE);

    ASSERT(session.show(true));
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (::IsIconic(search.found) == FALSE)
            break;
        sleep_ms(20);
    }
    ASSERT(::IsIconic(search.found) == FALSE);

    session.close();
    return true;
}

} // namespace

int main()
{
    std::wcout << L"=== ConPTY Pseudo Window ===" << std::endl;
    RUN_TEST(pseudo_window_contract, L"GetConsoleWindow pseudo window");
    RUN_TEST(pseudo_window_preconnect_signals, L"Pseudo window pre-connect signals");
    RUN_TEST(pseudo_window_after_connect, L"Pseudo window signals after connect");
    RUN_TEST(interactive_pwsh_preconnect_signals, L"Interactive pwsh pre-connect signals");
    RUN_TEST(interactive_pwsh_survives_signals, L"Interactive pwsh signals");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}