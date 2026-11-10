// tests/test_conpty_session_selftest.cpp
// ConPTY 会话驱动的自测：真实客户端、输出捕获、退出码、超时清理。
#include "lib/conpty_session.hpp"
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <cstdio>

namespace
{

corehost::test::result client_output_and_exit()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    const std::wstring command = L"\"" + cmd + L"\" /c \"echo conpty-marker & for /l %i in (1,1,20000) do @rem & exit /b 0\"";
    REQUIRE(session.start(command));

    REQUIRE(session.wait_output("conpty-marker", 10000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

corehost::test::result resize_keeps_session_alive()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(L"\"" + cmd + L"\" /c \"for /l %i in (1,1,20000) do @rem & exit /b 0\""));
    REQUIRE(session.resize(COORD{100, 30}));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"client_output", "conpty", &client_output_and_exit},
        {"resize", "conpty", &resize_keeps_session_alive},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== conpty session self test ===\n");
    const auto stats = corehost::test::run_isolated(cases, {}, 60000);
    std::fwprintf(stdout, L"  %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);
    return corehost::test::exit_code(stats);
}
