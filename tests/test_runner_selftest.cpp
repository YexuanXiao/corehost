// tests/test_runner_selftest.cpp
// 测试库 runner 的自测（design/0003 要求：runner 自身必须有覆盖）。
#include "lib/runner.hpp"

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
    SKIP(L"模拟前置条件不满足");
    return corehost::test::result::pass;
}

bool filter_selects_by_name_and_tag()
{
    constexpr corehost::test::test_case cases[] = {
        {"alpha", "unit", &case_pass},
        {"beta", "slow", &case_pass},
    };

    const auto by_name = corehost::test::run(cases, "alpha");
    const auto by_tag = corehost::test::run(cases, "slow");
    const auto all = corehost::test::run(cases);
    return by_name.passed == 1 && by_name.failed == 0 && by_tag.passed == 1 && all.passed == 2;
}

} // namespace

int main()
{
    constexpr corehost::test::test_case cases[] = {
        {"alpha", "unit", &case_pass},
        {"beta", "slow", &case_pass},
    };
    const corehost::test::test_case outcomes[] = {
        {"passing", "selftest", &case_pass},
        {"failing", "selftest", &case_fail},
        {"skipping", "selftest", &case_skip},
    };

    std::fwprintf(stdout, L"=== runner self test ===\n");
    const auto stats = corehost::test::run(outcomes);
    std::fwprintf(stdout, L"  stats: %d passed, %d failed, %d skipped\n", stats.passed, stats.failed, stats.skipped);

    bool ok = stats.passed == 1 && stats.failed == 1 && stats.skipped == 1;
    ok = ok && filter_selects_by_name_and_tag();
    ok = ok && corehost::test::run(cases).passed == 2;

    std::fwprintf(stdout, L"  runner self test: %ls\n", ok ? L"PASS" : L"FAIL");
    return ok ? 0 : 1;
}
