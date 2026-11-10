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
    DWORD mode = 0;
    const BOOL got_mode = ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode);

    corehost::test::unbind_console_connection();

    // 清理：先断开客户端连接，再终止宿主。
    session.connection = win32::handle{};
    ::TerminateProcess(host.hProcess, 0);
    ::WaitForSingleObject(host.hProcess, 5000);
    ::CloseHandle(host.hThread);
    ::CloseHandle(host.hProcess);

    REQUIRE(got_mode != FALSE);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"connect", "defterm", &connect_and_call_console_api},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== defterm connect self test ===\n");
    const auto stats = corehost::test::run_isolated(cases, {}, 60000);
    std::fwprintf(stdout, L"  %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);
    return corehost::test::exit_code(stats);
}
