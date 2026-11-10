// tests/test_conpty_differential.cpp
// 与系统 conhost 的差分测试：同一组控制台 API 分别在 corehost 与 conhost 宿主下
// 执行，比较**稳定、有意义**的返回值。
//
// 机制：libconpty 按"测试可执行文件同目录的 corehost.exe"选择宿主
// （libconpty/libconpty.hpp:189-211）。把系统 conhost.exe 复制成 corehost.exe
// 放进独立目录，同一会话就会跑在系统 conhost 上，无需注册表或服务。
//
// conhost 自身有已知 bug，这里只比较以下字段（其余仅记录，不 1:1 对齐）：
//   in_mode / out_mode / attr_after / cursor_size / cursor_visible / title
// 尺寸与光标坐标会受换行/重绘差异影响，只打印不比较。
#include "test_common.hpp"

#include "lib/conpty_session.hpp"
#include "lib/platform.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <cwchar>

using namespace corehost::test;

namespace
{

std::wstring module_path()
{
    wchar_t module[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, module, ARRAYSIZE(module)) == 0)
        return {};
    return module;
}

std::wstring quoted(const std::wstring &text)
{
    return L"\"" + text + L"\"";
}

// 子进程模式：在"当前目录的 corehost.exe"宿主上跑 API 探针，结果写 JSON。
int run_scenario()
{
    const auto probe = quote_self_dir(L"console_probe.exe");
    if (probe.empty())
        return 3;

    const std::filesystem::path output = std::filesystem::current_path() / L"result.json";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    conpty_session session;
    if (!session.start(quoted(probe) + L" --api-probe " + quoted(output.wstring())))
        return 4;

    const bool exited = session.wait_exit(30000);
    session.close();
    return exited && std::filesystem::exists(output) ? 0 : 5;
}

std::map<std::string, std::string> read_json(const std::filesystem::path &path)
{
    std::map<std::string, std::string> values;
    std::ifstream file{path};
    if (!file)
        return values;

    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t position = 0;
    while ((position = text.find('"', position)) != std::string::npos)
    {
        const size_t key_end = text.find('"', position + 1);
        if (key_end == std::string::npos)
            break;
        const std::string key = text.substr(position + 1, key_end - position - 1);
        const size_t colon = text.find(':', key_end);
        if (colon == std::string::npos)
            break;

        size_t value_begin = colon + 1;
        while (value_begin < text.size() && (text[value_begin] == ' ' || text[value_begin] == '\t'))
            ++value_begin;

        std::string value;
        if (value_begin < text.size() && text[value_begin] == '"')
        {
            const size_t value_end = text.find('"', value_begin + 1);
            if (value_end == std::string::npos)
                break;
            value = text.substr(value_begin + 1, value_end - value_begin - 1);
            position = value_end + 1;
        }
        else
        {
            const size_t value_end = text.find_first_of(",}", value_begin);
            value = text.substr(value_begin, value_end - value_begin);
            position = value_end == std::string::npos ? text.size() : value_end;
        }
        values[key] = value;
    }
    return values;
}

bool prepare_host_dirs(const std::filesystem::path &base, std::filesystem::path &corehost_dir,
                       std::filesystem::path &conhost_dir)
{
    std::error_code error;
    const auto self = std::filesystem::path{module_path()};
    const auto self_dir = self.parent_path();
    corehost_dir = base / L"corehost";
    conhost_dir = base / L"conhost";
    std::filesystem::create_directories(corehost_dir, error);
    std::filesystem::create_directories(conhost_dir, error);
    if (error)
        return false;

    // 两个目录都需要：测试 exe（子进程模式）与 console_probe.exe。
    for (const auto &dir : {corehost_dir, conhost_dir})
    {
        std::filesystem::copy_file(self, dir / self.filename(), std::filesystem::copy_options::overwrite_existing,
                                   error);
        std::filesystem::copy_file(self_dir / L"console_probe.exe", dir / L"console_probe.exe",
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error)
            return false;
    }

    std::filesystem::copy_file(self_dir / L"corehost.exe", corehost_dir / L"corehost.exe",
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error)
        return false;

    wchar_t windows[MAX_PATH]{};
    if (::GetWindowsDirectoryW(windows, ARRAYSIZE(windows)) == 0)
        return false;
    const auto system_conhost = std::filesystem::path{windows} / L"System32" / L"conhost.exe";
    if (!std::filesystem::exists(system_conhost))
        return false;

    // 系统 conhost 改名成 corehost.exe：libconpty 按名查找宿主。
    std::filesystem::copy_file(system_conhost, conhost_dir / L"corehost.exe",
                               std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool run_in_dir(const std::filesystem::path &dir, DWORD &exit_code)
{
    std::wstring command = quoted((dir / std::filesystem::path{module_path()}.filename()).wstring()) +
                           L" --scenario api";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                         dir.c_str(), &startup, &process) == FALSE)
        return false;

    const DWORD wait = ::WaitForSingleObject(process.hProcess, 60000);
    if (wait != WAIT_OBJECT_0)
        ::TerminateProcess(process.hProcess, 1);
    ::GetExitCodeProcess(process.hProcess, &exit_code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 && exit_code == 0;
}

void print_values(const char *title, const std::map<std::string, std::string> &values)
{
    std::printf("%s:", title);
    for (const auto &[key, value] : values)
        std::printf(" %s=%s", key.c_str(), value.c_str());
    std::printf("\n");
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc > 1 && ::wcscmp(argv[1], L"--scenario") == 0)
        return run_scenario();

    std::printf("=== ConPTY Differential (corehost vs conhost) ===\n");

    const auto base = std::filesystem::path{module_path()}.parent_path() / L"differential";
    std::filesystem::path corehost_dir;
    std::filesystem::path conhost_dir;
    if (!prepare_host_dirs(base, corehost_dir, conhost_dir))
    {
        std::printf("  [skip] cannot prepare host directories or system conhost.exe missing\n");
        std::printf("  0 passed, 0 failed, 1 skipped\n");
        return 0;
    }

    DWORD corehost_exit = 0;
    DWORD conhost_exit = 0;
    if (!run_in_dir(corehost_dir, corehost_exit) || !run_in_dir(conhost_dir, conhost_exit))
    {
        std::printf("  FAILED: scenario run failed (corehost exit=%lu conhost exit=%lu)\n",
                    static_cast<unsigned long>(corehost_exit), static_cast<unsigned long>(conhost_exit));
        return 1;
    }

    const auto corehost_values = read_json(corehost_dir / L"result.json");
    const auto conhost_values = read_json(conhost_dir / L"result.json");
    if (corehost_values.empty() || conhost_values.empty())
    {
        std::printf("  FAILED: missing result.json\n");
        return 1;
    }

    print_values("corehost", corehost_values);
    print_values("conhost", conhost_values);

    // 只比较稳定、有意义的行为；换行/重绘/尺寸差异只记录（conhost 已知 bug）。
    // out_mode 不比较：conhost 会读用户注册表 VirtualTerminalLevel 决定是否默认
    // 打开 ENABLE_VIRTUAL_TERMINAL_PROCESSING（terminal/src/host/screenInfo.cpp），
    // 属于机器配置差异，不是实现差异。
    const char *compared[] = {"in_mode", "attr_after", "cursor_size", "cursor_visible", "title", "title_len"};
    int mismatches = 0;
    for (const char *key : compared)
    {
        const auto host_it = corehost_values.find(key);
        const auto reference_it = conhost_values.find(key);
        if (host_it == corehost_values.end() || reference_it == conhost_values.end())
        {
            std::printf("  MISSING: %s\n", key);
            ++mismatches;
            continue;
        }
        if (host_it->second != reference_it->second)
        {
            std::printf("  DIFF %s: corehost=%s conhost=%s\n", key, host_it->second.c_str(),
                        reference_it->second.c_str());
            ++mismatches;
        }
    }

    std::printf("  %s\n", mismatches == 0 ? "PASSED" : "FAILED");
    return mismatches == 0 ? 0 : 1;
}
