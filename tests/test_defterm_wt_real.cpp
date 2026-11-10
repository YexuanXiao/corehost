// tests/test_defterm_wt_real.cpp
// L2：真实 WindowsTerminal 层（有机会才跑）。
//
// 不做任何注册表写入：只读当前用户的默认终端配置（HKCU\Console\%%Startup）。
// 如果默认终端是 WindowsTerminal，就用 CREATE_NEW_CONSOLE 启动一个普通控制台
// 程序——Windows 会把这次控制台创建委托给默认终端（true defterm handoff），
// 由 WT 托管这个控制台。用例只断言"命令确实在委托后的控制台里执行了"。
//
// 默认终端是 conhost、或该键不存在时，用例 SKIP：这条路径依赖机器配置，
// 不属于本仓库可控的状态。
#include "test_common.hpp"

#include "lib/platform.hpp"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace corehost::test;

namespace
{

std::wstring quote(const std::wstring &text)
{
    return L"\"" + text + L"\"";
}

constexpr wchar_t wt_console_clsid[] = L"{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}";
constexpr wchar_t conhost_clsid[] = L"{B23D10C0-E52E-411E-9D5B-C09FDF709C7D}";

// 读当前用户的默认终端（委托）CLSID；不存在返回空。
std::wstring default_terminal_clsid()
{
    wchar_t buffer[128]{};
    DWORD size = sizeof(buffer);
    const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, L"Console\\%%Startup", L"DelegationConsole",
                                          RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS)
        return {};
    return buffer;
}

bool wait_for_file(const std::filesystem::path &path, DWORD timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (std::filesystem::exists(path))
            return true;
        ::Sleep(100);
    }
    return false;
}

bool read_file(const std::filesystem::path &path)
{
    FILE *file = nullptr;
    if (::_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr)
        return false;
    char buffer[256]{};
    const size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    return std::string{buffer, read}.find("wt-handoff-marker") != std::string::npos;
}

// 默认终端是 WT 时，普通控制台程序的控制台创建会被委托给 WT。
bool console_creation_is_delegated_to_terminal()
{
    const std::wstring clsid = default_terminal_clsid();
    if (clsid.empty())
    {
        std::wcout << L"    [skip] no per-user default terminal configured" << std::endl;
        return true;
    }
    if (_wcsicmp(clsid.c_str(), conhost_clsid) == 0)
    {
        std::wcout << L"    [skip] default terminal is conhost" << std::endl;
        return true;
    }
    if (_wcsicmp(clsid.c_str(), wt_console_clsid) != 0)
    {
        std::wcout << L"    [skip] default terminal is a third-party terminal" << std::endl;
        return true;
    }

    // 放在测试可执行文件旁：%TEMP% 在受限环境下可能不可写。
    wchar_t module[MAX_PATH]{};
    ASSERT(::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) != 0);
    const auto marker_path = std::filesystem::path{module}.parent_path() / L"wt-handoff-marker.txt";
    std::error_code ignored;
    std::filesystem::remove(marker_path, ignored);

    const auto cmd = system32_path(L"cmd.exe");
    ASSERT(!cmd.empty());

    std::wstring command = quote(cmd) + L" /c echo wt-handoff-marker> \"" + marker_path.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // CREATE_NEW_CONSOLE：没有 pseudoconsole，走系统默认终端的委托路径。
    ASSERT(::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr,
                            &startup, &process) != FALSE);
    ::CloseHandle(process.hThread);

    const DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.hProcess, &exit_code);
    ::CloseHandle(process.hProcess);

    const bool file_exists = wait_for_file(marker_path, 10000);
    const bool content_ok = file_exists && read_file(marker_path);
    std::filesystem::remove(marker_path, ignored);

    std::wcout << L"    wait=" << wait << L" exit=0x" << std::hex << exit_code << std::dec
               << L" file=" << file_exists << L" content=" << content_ok << std::endl;
    ASSERT(wait == WAIT_OBJECT_0);
    ASSERT(exit_code == 0);
    ASSERT(file_exists);
    ASSERT(content_ok);
    return true;
}

} // namespace

int main()
{
    std::wcout << L"=== Defterm Real Terminal (WT) ===" << std::endl;
    RUN_TEST(console_creation_is_delegated_to_terminal, L"Console creation delegated to WindowsTerminal");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, "
               << (tests_passed + tests_failed) << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
