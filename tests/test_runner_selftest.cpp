// tests/test_runner_selftest.cpp
// 测试库 runner 的自测（design/0003 要求：runner 自身必须有覆盖）。
//
// 覆盖：同进程执行的通过/失败/跳过/过滤、子进程隔离的退出码汇总、超时终止。
#include "lib/runner.hpp"

#include <chrono>
#include <cstdio>

namespace
{

corehost::test::result case_pass()
{
    REQUIRE(1 + 1 == 2);
    return corehost::test::result::pass;
}

corehost::test::result case_fail()
{
    REQUIRE(1 + 1 == 3);
    return corehost::test::result::pass; // 不会执行到
}

corehost::test::result case_skip()
{
    SKIP(L"simulated unmet precondition");
    return corehost::test::result::pass;
}

corehost::test::result case_hang()
{
    // 故意挂起：用于验证父进程的超时终止。
    ::Sleep(10000);
    return corehost::test::result::pass;
}

bool same_process_semantics()
{
    constexpr corehost::test::test_case cases[] = {
        {"alpha", "unit", &case_pass},
        {"beta", "slow", &case_pass},
        {"skipping", "unit", &case_skip},
        {"failing", "unit", &case_fail},
    };

    const auto all = corehost::test::run(cases);
    if (all.passed != 2 || all.skipped != 1 || all.failed != 1)
        return false;

    const auto by_name = corehost::test::run(cases, "alpha");
    const auto by_tag = corehost::test::run(cases, "slow");
    return by_name.passed == 1 && by_name.failed == 0 && by_tag.passed == 1 && by_tag.failed == 0;
}

} // namespace

int main()
{
    // 一张表包含全部用例：子进程只按索引执行，父进程用标签过滤。
    constexpr corehost::test::test_case cases[] = {
        {"passing", "selftest", &case_pass},
        {"failing", "selftest", &case_fail},
        {"skipping", "selftest", &case_skip},
        {"hanging", "hang", &case_hang},
    };

    // 子进程模式：父进程会带 --case-index 重启本程序。
    if (const auto child = corehost::test::run_child_case(cases))
        return *child;

    std::fwprintf(stdout, L"=== runner self test ===\n");

    bool ok = same_process_semantics();

    // 子进程隔离：退出码 0/1/2 分别计为通过/失败/跳过。
    const auto isolated = corehost::test::run_isolated(cases, "selftest", 5000);
    std::fwprintf(stdout, L"  isolated: %d passed, %d failed, %d skipped\n", isolated.passed, isolated.failed,
                  isolated.skipped);
    ok = ok && isolated.passed == 1 && isolated.failed == 1 && isolated.skipped == 1;

    // 超时：挂起的用例必须在超时后被终止并记为失败，且不能拖到用例自己醒来。
    const auto start = std::chrono::steady_clock::now();
    const auto timed_out = corehost::test::run_isolated(cases, "hang", 800);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::fwprintf(stdout, L"  timeout: %d failed, elapsed=%lldms\n", timed_out.failed,
                  static_cast<long long>(elapsed.count()));
    ok = ok && timed_out.failed == 1 && elapsed.count() < 5000;

    std::fwprintf(stdout, L"  runner self test: %ls\n", ok ? L"PASS" : L"FAIL");
    return ok ? 0 : 1;
}
