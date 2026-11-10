// tests/lib/runner.hpp
// conhost 测试库的最小 runner。
//
// 设计见 doc/design/0003：不引入第三方框架（禁止联网），用例用表驱动方式
// 注册（不用静态初始化，避免顺序问题），断言失败直接返回当前用例，跳过
// 用 SKIP 表达。隔离/子进程执行与超时是后续步骤（C1 剩余部分）。
//
// 用法：
//   static corehost::test::result my_case() {
//       REQUIRE(1 + 1 == 2);
//       SKIP("Windows Terminal 未安装");
//       return corehost::test::result::pass;
//   }
//   int main() {
//       const corehost::test::test_case cases[] = {{"math", "unit", &my_case}};
//       return corehost::test::run(cases);
//   }
#pragma once
#include <array>
#include <cstdio>
#include <cwchar>
#include <span>
#include <string>
#include <string_view>

namespace corehost::test
{

enum class result
{
    pass,
    fail,
    skip,
};

struct test_case
{
    const char *name;
    const char *tags;
    result (*run)();
};

struct run_stats
{
    int passed = 0;
    int failed = 0;
    int skipped = 0;
};

namespace details
{

// 当前用例名，供断言输出定位；runner 在调用用例前设置。
inline std::string_view &current_case() noexcept
{
    static std::string_view name;
    return name;
}

inline void report_failure(const char *expression, const char *file, int line)
{
    std::fwprintf(stderr, L"  FAILED %hs: %hs (%hs:%d)\n", current_case().data(), expression, file, line);
}

} // namespace details

// 运行用例表。filter 非空时只跑 name 或 tags 包含该子串的用例。
// 返回 stats，便于自测与上层汇总；main 里通常用 exit_code(stats)。
inline run_stats run(std::span<const test_case> cases, std::string_view filter = {})
{
    run_stats stats{};
    for (const auto &test : cases)
    {
        const std::string_view name{test.name};
        const std::string_view tags{test.tags};
        if (!filter.empty() && name.find(filter) == std::string::npos && tags.find(filter) == std::string::npos)
            continue;

        details::current_case() = name;
        std::fwprintf(stdout, L"  %hs ... ", test.name);

        const auto outcome = test.run();
        switch (outcome)
        {
        case result::pass:
            std::fwprintf(stdout, L"PASS\n");
            ++stats.passed;
            break;
        case result::skip:
            std::fwprintf(stdout, L"SKIP\n");
            ++stats.skipped;
            break;
        case result::fail:
            std::fwprintf(stdout, L"FAIL\n");
            ++stats.failed;
            break;
        }
        details::current_case() = {};
    }
    return stats;
}

inline int exit_code(const run_stats &stats) noexcept
{
    return stats.failed == 0 ? 0 : 1;
}

} // namespace corehost::test

// REQUIRE：条件不成立时记录并结束当前用例（等价于 GTest 的 ASSERT）。
#define REQUIRE(cond)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ::corehost::test::details::report_failure(#cond, __FILE__, __LINE__);                                    \
            return ::corehost::test::result::fail;                                                                     \
        }                                                                                                              \
    } while (false)

// SKIP：环境前置不满足时跳过当前用例，输出原因（不计为通过）。
#define SKIP(reason)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        std::fwprintf(stdout, L"[skip] %ls ", static_cast<const wchar_t *>(reason));                                   \
        return ::corehost::test::result::skip;                                                                         \
    } while (false)
