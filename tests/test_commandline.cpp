
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <shellapi.h>

#include "cli/console_arguments.hpp"
#include "utility/crtdbg.hpp"
#include "test_common.hpp"

#include <iostream>
#include <random>
#include <format>
#include <vector>
#include <string_view>

[[nodiscard]] std::vector<std::wstring> split_command_line(const win32::wcstring_view command_line)
{
    std::vector<std::wstring> args;
    win32::command_line_view parser(command_line);
    for (auto token : parser)
    {
        args.emplace_back(token);
    }
    return args;
}

[[nodiscard]] std::vector<std::wstring> split_command_line_win32(const win32::wcstring_view command_line)
{
    int argc = 0;
    auto argv = ::CommandLineToArgvW(command_line.c_str(), &argc);
    if (!argv)
        win32::throw_last_error();
    std::vector<std::wstring> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    ::LocalFree(argv);
    return args;
}

bool run_fuzz()
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<std::size_t> len_dist(1, 200);
    std::uniform_int_distribution<unsigned int> char_dist(32, 126);

    for (int i = 0; i < 10'000; ++i)
    {
        std::wstring s;
        auto len = len_dist(rng);
        s.reserve(len);
        for (std::size_t j = 0; j < len; ++j)
            s += static_cast<wchar_t>(char_dist(rng));

        auto custom = split_command_line(s);
        auto win32 = split_command_line_win32(s);

        if (custom.size() != win32.size())
            return false;
        for (std::size_t j = 0; j < custom.size(); ++j)
            if (custom[j] != win32[j])
                return false;
    }
    return true;
}

int main()
{
    utility::suppress_crt_error_dialogs();
    std::wcout << L"=== 命令行解析单元测试 ===\n\n";
    RUN_TEST(run_fuzz, L"命令行解析模糊测试 (10000组)");
    std::wcout << L"\n=== 总计 ===\n" << L"通过: " << tests_passed << L"\n" << L"失败: " << tests_failed << L"\n";
    return tests_failed > 0 ? 1 : 0;
}
