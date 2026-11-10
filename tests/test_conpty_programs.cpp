// tests/test_conpty_programs.cpp
// ConPTY 下的真实程序用例：cmd、嵌套子进程、powershell、vim，以及输入时延。
//
// 设计见 doc/plan/04-conpty-tests.md：每用例独立会话、条件+截止时间等待、
// 外部程序缺失时 SKIP 而不是假装通过。
#include "lib/conpty_session.hpp"
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace
{

std::wstring quote(std::wstring_view path)
{
    return L"\"" + std::wstring{path} + L"\"";
}

corehost::test::result cmd_echo_and_exit()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"echo cmd-marker\""));

    REQUIRE(session.wait_output("cmd-marker", 15000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// 控制台由 cmd 再拉起一个 cmd：子进程必须继承同一个控制台才能输出。
corehost::test::result nested_child_inherits_console()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd) + L" /c \"cmd /c echo nested-marker\""));

    REQUIRE(session.wait_output("nested-marker", 15000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

corehost::test::result powershell_command()
{
    const auto powershell = corehost::test::system32_path(L"WindowsPowerShell\\v1.0\\powershell.exe");
    if (!corehost::test::file_exists(powershell))
        SKIP(L"powershell.exe not found");

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(powershell) + L" -NoLogo -NoProfile -Command \"Write-Output ps-marker\""));

    REQUIRE(session.wait_output("ps-marker", 20000));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(20000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

corehost::test::result vim_starts_and_quits()
{
    const auto vim = corehost::test::find_on_path(L"vim.exe");
    if (vim.empty())
        SKIP(L"vim.exe not on PATH");

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(vim) + L" -u NONE -N -n"));

    // 等 vim 完成初始化再发退出命令（没有可见的稳定标记，用输出非空判断）。
    REQUIRE(session.wait_output("\x1b", 15000));

    REQUIRE(session.write("\x1b:qa!\r"));

    DWORD exit_code = static_cast<DWORD>(-1);
    REQUIRE(session.wait_exit(15000, &exit_code));
    REQUIRE(exit_code == 0);
    return corehost::test::result::pass;
}

// A10：输入到回显的时延（不轮询模型的回归指标）。
corehost::test::result input_latency()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    corehost::test::conpty_session session;
    REQUIRE(session.start(quote(cmd)));

    // 等提示符出现，确保会话已就绪。
    REQUIRE(session.wait_output(">", 15000));
    (void)session.take_output();

    std::vector<long long> samples;
    samples.reserve(20);

    for (int i = 0; i < 20; ++i)
    {
        (void)session.take_output();
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(session.write("a"));
        REQUIRE(session.wait_output("a", 5000));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        samples.push_back(static_cast<long long>(elapsed.count()));
    }

    session.write("\x03");
    std::sort(samples.begin(), samples.end());
    const auto p95 = samples[samples.size() * 95 / 100];
    std::fwprintf(stdout, L"[latency] p95=%lldms max=%lldms ", p95, samples.back());

    REQUIRE(p95 < 250);
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"cmd", "conpty;program", &cmd_echo_and_exit},
        {"nested", "conpty;program;inherit", &nested_child_inherits_console},
        {"powershell", "conpty;program", &powershell_command},
        {"vim", "conpty;program;interactive", &vim_starts_and_quits},
        {"latency", "conpty;perf", &input_latency},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    const auto stats = corehost::test::run_isolated(cases, {}, 120000);
    std::fwprintf(stdout, L"=== conpty programs: %d passed, %d failed, %d skipped ===\n", stats.passed, stats.failed,
                  stats.skipped);
    return corehost::test::exit_code(stats);
}
