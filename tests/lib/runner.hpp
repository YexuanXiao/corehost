// tests/lib/runner.hpp
// conhost 测试库的最小 runner。
//
// 设计见 doc/design/0003：不引入第三方框架（禁止联网）。用例用表驱动方式
// 注册（不用静态初始化，避免顺序问题），断言失败直接结束当前用例，跳过用
// SKIP 表达。
//
// 两种执行方式：
// 1. 同进程：corehost::test::run(cases) —— 快，但一个用例挂起会拖死整组。
// 2. 子进程隔离：corehost::test::run_isolated(cases, timeout) —— 每个用例重启
//    自身并带上 --case-index，父进程负责超时终止与汇总。默认推荐后者。
//
// 用法：
//   static corehost::test::result my_case() {
//       REQUIRE(1 + 1 == 2);
//       SKIP(L"Windows Terminal 未安装");
//       return corehost::test::result::pass;
//   }
//   int main(int argc, wchar_t **argv) {
//       const corehost::test::test_case cases[] = {{"math", "unit", &my_case}};
//       if (const auto code = corehost::test::run_child_case(cases, argc, argv))
//           return *code;
//       const auto stats = corehost::test::run_isolated(cases);
//       return corehost::test::exit_code(stats);
//   }
#pragma once
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <cwchar>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace corehost::test
{

// 测试进程统一抑制系统错误对话框：被测程序初始化失败（0xc0000142 等）时
// 只应表现为退出码，不允许在用户桌面上弹窗。
inline const bool error_dialogs_suppressed = [] {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    return true;
}();

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

inline bool matches(const test_case &test, std::string_view filter) noexcept
{
    if (filter.empty())
        return true;
    return std::string_view{test.name}.find(filter) != std::string_view::npos ||
           std::string_view{test.tags}.find(filter) != std::string_view::npos;
}

// 子进程退出码：0=通过，1=失败，2=跳过。
inline int to_exit_code(result outcome) noexcept
{
    switch (outcome)
    {
    case result::pass:
        return 0;
    case result::skip:
        return 2;
    case result::fail:
        return 1;
    }
    return 1;
}

} // namespace details

// 运行用例表（同进程）。filter 非空时只跑 name 或 tags 包含该子串的用例。
inline run_stats run(std::span<const test_case> cases, std::string_view filter = {})
{
    run_stats stats{};
    for (const auto &test : cases)
    {
        if (!details::matches(test, filter))
            continue;

        details::current_case() = test.name;
        std::fwprintf(stdout, L"  %hs ... ", test.name);

        switch (test.run())
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

// 子进程模式：命令行是 "--case-index N" 时只跑第 N 个用例并返回退出码。
// 其它命令行返回 nullopt，调用方按父进程处理。
inline std::optional<int> run_child_case(std::span<const test_case> cases)
{
    // 直接从命令行解析，不依赖 main/wmain 的 argv 类型（wchar_t** 配 main 会让
    // CRT 把 ANSI 字节当成宽字符，越界读）。
    int argc = 0;
    LPWSTR *argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        std::fwprintf(stderr, L"run_child_case: CommandLineToArgvW failed: %lu\n", ::GetLastError());
        return 1;
    }

    std::optional<int> child_result;
    if (argc == 3 && std::wstring_view{argv[1]} == L"--case-index")
    {
        wchar_t *end = nullptr;
        const auto index = std::wcstoul(argv[2], &end, 10);
        if (end == argv[2] || *end != L'\0' || index >= cases.size())
        {
            child_result = 2;
        }
        else
        {
            const auto &test = cases[index];
            details::current_case() = test.name;
            const auto outcome = test.run();
            details::current_case() = {};
            std::fwprintf(stdout, L"  %hs ... %ls\n", test.name,
                          outcome == result::pass ? L"PASS" : (outcome == result::skip ? L"SKIP" : L"FAIL"));
            child_result = details::to_exit_code(outcome);
        }
    }

    ::LocalFree(argv);
    return child_result;
}

// 把本用例期间写出的 corehost 日志复制到 test-artifacts/<case>/。
// 偶发失败如果没有产物，就只能靠复跑碰运气；这里至少保住会话日志。
inline void archive_case_logs(const char *case_name, std::filesystem::file_time_type since)
{
    std::error_code error;
    wchar_t module[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) == 0)
        return;

    const std::filesystem::path base = std::filesystem::path{module}.parent_path();
    const std::filesystem::path logs = base / L"logs";
    if (!std::filesystem::exists(logs, error))
        return;

    const std::filesystem::path destination = base / L"test-artifacts" / std::filesystem::path{case_name};
    std::filesystem::create_directories(destination, error);

    for (const auto &entry : std::filesystem::directory_iterator(logs, error))
    {
        std::error_code time_error;
        const auto written = std::filesystem::last_write_time(entry, time_error);
        if (time_error || written < since)
            continue;

        std::error_code copy_error;
        std::filesystem::copy_file(entry, destination / entry.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
    }
}

// timeout_ms 是测试基础设施的判定期限：超时即终止子进程并记为失败，避免一个
// 挂起的用例拖死整组（产品代码不允许等待上限，测试基础设施需要它来判定失败）。
// 子进程隔离执行：每个用例重启自身（--case-index N），父进程等待并汇总。
inline run_stats run_isolated(std::span<const test_case> cases, std::string_view filter = {}, DWORD timeout_ms = 60000)
{
    run_stats stats{};

    wchar_t module_path[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path)) == 0)
    {
        std::fwprintf(stderr, L"run_isolated: GetModuleFileNameW failed: %lu\n", ::GetLastError());
        ++stats.failed;
        return stats;
    }

    for (size_t index = 0; index < cases.size(); ++index)
    {
        const auto &test = cases[index];
        if (!details::matches(test, filter))
            continue;

        std::wstring command = L"\"";
        command += module_path;
        command += L"\" --case-index ";
        command += std::to_wstring(index);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};

        // 父进程没有可见控制台时（例如 Visual Studio 测试宿主），子进程默认会
        // 新建一个可见控制台窗口（conhost 闪窗）；用一个无窗口控制台代替。
        // 父进程有控制台时保持继承，保留隔离用例的失败诊断输出。
        const DWORD creation_flags = (::GetConsoleWindow() == nullptr) ? CREATE_NO_WINDOW : 0;
        if (::CreateProcessW(module_path, command.data(), nullptr, nullptr, FALSE, creation_flags, nullptr, nullptr,
                             &startup, &process) == FALSE)
        {
            std::fwprintf(stderr, L"  %hs ... CREATE FAILED (err=%lu)\n", test.name, ::GetLastError());
            ++stats.failed;
            continue;
        }

        const auto case_start = std::filesystem::file_time_type::clock::now();
        const DWORD wait = ::WaitForSingleObject(process.hProcess, timeout_ms);
        DWORD exit_code_value = 1;
        bool case_failed = false;
        if (wait == WAIT_TIMEOUT)
        {
            // 超时：终止并记录，不能让它继续占用。
            ::TerminateProcess(process.hProcess, 1);
            ::WaitForSingleObject(process.hProcess, INFINITE);
            std::fwprintf(stdout, L"  %hs ... TIMEOUT\n", test.name);
            ++stats.failed;
            case_failed = true;
        }
        else if (wait != WAIT_OBJECT_0 || ::GetExitCodeProcess(process.hProcess, &exit_code_value) == FALSE)
        {
            std::fwprintf(stderr, L"  %hs ... WAIT FAILED (err=%lu)\n", test.name, ::GetLastError());
            ++stats.failed;
            exit_code_value = 1;
            case_failed = true;
        }
        else if (exit_code_value == 0)
        {
            ++stats.passed;
        }
        else if (exit_code_value == 2)
        {
            ++stats.skipped;
        }
        else
        {
            // 子进程的 stdout 在 CI 里可能被吞掉；父进程必须显式报告失败用例名
            // 和退出码，否则只剩一行总汇总无法定位。
            std::fwprintf(stderr, L"  %hs ... FAIL (exit=%lu)\n", test.name,
                          static_cast<unsigned long>(exit_code_value));
            ++stats.failed;
            case_failed = true;
        }

        // 失败时归档本用例期间产生的 corehost 日志，偶发失败才有材料可查。
        if (case_failed)
            archive_case_logs(test.name, case_start);

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }

    return stats;
}

} // namespace corehost::test

// REQUIRE：条件不成立时记录并结束当前用例（等价于 GTest 的 ASSERT）。
#define REQUIRE(cond)                                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ::corehost::test::details::report_failure(#cond, __FILE__, __LINE__);                                      \
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