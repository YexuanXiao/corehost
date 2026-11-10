// tests/test_platform_selftest.cpp
// 测试库宿主层（环境探测 + 子进程运行器）的自测。
#include "lib/platform.hpp"
#include "lib/runner.hpp"

#include <chrono>
#include <cstdio>

namespace
{

std::wstring quote_self()
{
    wchar_t module[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) == 0)
        return {};
    return L"" + std::wstring{module} + L"";
}

corehost::test::result tool_output_is_captured()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    const auto result = corehost::test::run_tool(cmd, L"/c echo hello-from-test", 10000);
    REQUIRE(!result.timed_out);
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.out.find("hello-from-test") != std::string::npos);
    return corehost::test::result::pass;
}

corehost::test::result exit_code_is_propagated()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    const auto result = corehost::test::run_tool(cmd, L"/c exit 3", 10000);
    REQUIRE(!result.timed_out);
    REQUIRE(result.exit_code == 3);
    return corehost::test::result::pass;
}

corehost::test::result timeout_terminates_child()
{
    const auto cmd = corehost::test::system32_path(L"cmd.exe");
    REQUIRE(!cmd.empty());

    const auto start = std::chrono::steady_clock::now();
    const auto result =
        corehost::test::run_process(quote_self() + L" --sleep-forever", 600);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.timed_out);
    REQUIRE(elapsed < 5000);
    return corehost::test::result::pass;
}

corehost::test::result environment_predicates_are_stable()
{
    // 这些谓词必须能在任意机器上安全求值（不存在的工具返回 false，不崩溃）。
    (void)corehost::test::has_ssh();
    (void)corehost::test::has_sshd();
    (void)corehost::test::has_windows_terminal();
    (void)corehost::test::is_interactive_session();

    REQUIRE(corehost::test::file_exists(corehost::test::system32_path(L"cmd.exe")));
    REQUIRE(!corehost::test::find_on_path(L"cmd.exe").empty());
    REQUIRE(corehost::test::find_on_path(L"definitely-not-a-tool-xyz.exe").empty());
    return corehost::test::result::pass;
}

} // namespace

int main()
{
    // 自派生模式：作为"长时间运行的子进程"被 timeout 用例创建，不依赖任何
    // 外部控制台程序（避免 0xc0000142 弹窗）。
    if (std::wstring_view{::GetCommandLineW()}.find(L"--sleep-forever") != std::wstring_view::npos)
    {
        ::Sleep(60000);
        return 0;
    }
    constexpr corehost::test::test_case cases[] = {
        {"tool_output", "platform", &tool_output_is_captured},
        {"exit_code", "platform", &exit_code_is_propagated},
        {"timeout", "platform", &timeout_terminates_child},
        {"environment", "platform", &environment_predicates_are_stable},
    };

    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== platform self test ===\n");
    const auto stats = corehost::test::run_isolated(cases);
    std::fwprintf(stdout, L"  %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);
    return corehost::test::exit_code(stats);
}
