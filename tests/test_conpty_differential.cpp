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
int run_scenario(std::wstring_view name)
{
    const auto probe = quote_self_dir(L"console_probe.exe");
    if (probe.empty())
        return 3;

    const std::filesystem::path output_path = std::filesystem::current_path() / L"result.json";
    std::error_code ignored;
    std::filesystem::remove(output_path, ignored);

    if (name == L"api")
    {
        conpty_session session;
        if (!session.start(quoted(probe) + L" --api-probe " + quoted(output_path.wstring())))
            return 4;

        const bool exited = session.wait_exit(30000);
        const auto api_output = session.take_output();
        const bool exists = std::filesystem::exists(output_path);
        session.close();
        if (!exited || !exists)
        {
            std::ofstream diag{std::filesystem::current_path() / L"diag.txt"};
            diag << "exited=" << (exited ? 1 : 0) << " exists=" << (exists ? 1 : 0) << " output=" << api_output << "\n";
        }
        return exited && exists ? 0 : 5;
    }

    // Ctrl+C 场景：探针把 ReadFile/ReadConsoleInput 的返回写进临时文件，
    // 这里再把结果转成 JSON，供两个宿主比较。
    //
    // 复现 corehost 与 conhost 的差异（G24）：
    //   - 探针以 --raw-read-probe / --input-record-probe 启动，打印 READY 后
    //     阻塞在 ReadFile / ReadConsoleInput；
    //   - 这里写入 Win32Input 的 Ctrl+C 序列 ESC[67;46;3;1;8;1_；
    //   - corehost：读以 STATUS_ALERTED 结束，客户端得到 0 字节/0 记录；
    //   - conhost：读不返回，探针不退出（read_ctrl 场景的 exited=0）。
    // 两个场景在父进程里只记录不比较：挂起永不返回比返回 0 字节差，
    // 不应把 conhost 的缺陷固化成 corehost 的期望。
    const bool raw = name == L"raw_ctrl";
    const std::wstring mode = raw ? L"--raw-read-probe" : L"--input-record-probe";
    const std::string ready = raw ? "RAW-PROBE-READY" : "INPUT-PROBE-READY";
    const auto marker = std::filesystem::current_path() / L"ctrl-marker.txt";
    std::filesystem::remove(marker, ignored);

    conpty_session session;
    if (!session.start(quoted(probe) + L" " + mode + L" " + quoted(marker.wstring())))
        return 6;
    if (!session.wait_output(ready, 15000))
    {
        session.close();
        return 7;
    }

    ::Sleep(300);
    if (!session.write("\x1b[67;46;3;1;8;1_"))
    {
        session.close();
        return 8;
    }

    // 只等 3s：这两个场景在 conhost 下不会返回，等太久只会拖慢测试。
    const bool exited = session.wait_exit(3000);
    const auto output = session.take_output();
    session.close();

    std::ifstream file{marker};
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
        text.pop_back();

    // 把探针输出也带上：失败时父进程可以直接看到宿主返回了什么。
    std::string escaped;
    for (const char ch : output)
    {
        if (ch == '\n' || ch == '\r' || ch == '"' || ch == '\\')
            continue;
        if (static_cast<unsigned char>(ch) >= 0x20 && static_cast<unsigned char>(ch) < 0x7F)
            escaped.push_back(ch);
    }

    std::ofstream out_file{output_path};
    out_file << "{\"" << (raw ? "raw" : "input") << "\":\"" << text << "\",\"exited\":" << (exited ? 1 : 0)
             << ",\"probe_out\":\"" << escaped << "\"}\n";
    // 这两个场景只记录：conhost 可能不结束，由父进程打印差异。
    return 0;
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

bool prepare_corehost_dir(const std::filesystem::path &base, std::filesystem::path &dir)
{
    std::error_code error;
    const auto self = std::filesystem::path{module_path()};
    const auto self_dir = self.parent_path();
    dir = base / L"corehost";
    std::filesystem::create_directories(dir, error);
    if (error)
        return false;

    std::filesystem::copy_file(self, dir / self.filename(), std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(self_dir / L"console_probe.exe", dir / L"console_probe.exe",
                               std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(self_dir / L"corehost.exe", dir / L"corehost.exe",
                               std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool prepare_conhost_dir(const std::filesystem::path &base, std::filesystem::path &dir)
{
    std::error_code error;
    const auto self = std::filesystem::path{module_path()};
    const auto self_dir = self.parent_path();
    dir = base / L"conhost";
    std::filesystem::create_directories(dir, error);
    if (error)
        return false;

    std::filesystem::copy_file(self, dir / self.filename(), std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(self_dir / L"console_probe.exe", dir / L"console_probe.exe",
                               std::filesystem::copy_options::overwrite_existing, error);

    wchar_t windows[MAX_PATH]{};
    if (::GetWindowsDirectoryW(windows, ARRAYSIZE(windows)) == 0)
        return false;
    const auto system_conhost = std::filesystem::path{windows} / L"System32" / L"conhost.exe";
    if (!std::filesystem::exists(system_conhost))
        return false;

    // 系统 conhost 改名成 corehost.exe：libconpty 按名查找宿主。
    std::filesystem::copy_file(system_conhost, dir / L"corehost.exe", std::filesystem::copy_options::overwrite_existing,
                               error);
    return !error;
}

bool env_enabled(const wchar_t *name) noexcept
{
    return ::GetEnvironmentVariableW(name, nullptr, 0) != 0;
}
bool run_in_dir(const std::filesystem::path &dir, std::wstring_view scenario, DWORD &exit_code)
{
    std::wstring command = quoted((dir / std::filesystem::path{module_path()}.filename()).wstring()) + L" --scenario " +
                           std::wstring{scenario};
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(),
                         &startup, &process) == FALSE)
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
        return run_scenario(argc >= 3 ? argv[2] : L"api");

    const auto base = std::filesystem::path{module_path()}.parent_path() / L"differential";

    // 默认只跑 corehost 侧冒烟，不与系统 conhost 比较；比较是显式 opt-in，
    // 因为需要复制系统 conhost 且 raw/record Ctrl+C 场景在 conhost 下会等待
    // 超时，整体较慢。选项说明见 doc/tasks/06-test-options.md。
    const bool compare = env_enabled(L"COREHOST_TEST_DIFFERENTIAL");
    const bool compare_ctrl = compare && env_enabled(L"COREHOST_TEST_DIFFERENTIAL_CTRL");

    if (!compare)
    {
        std::printf("=== ConPTY Differential (corehost-only smoke) ===\n");
        std::printf("  comparison: disabled (set COREHOST_TEST_DIFFERENTIAL=1 to compare with conhost)\n");
        std::printf("  raw/record Ctrl+C comparison: disabled (also needs COREHOST_TEST_DIFFERENTIAL_CTRL=1)\n");

        std::filesystem::path corehost_dir;
        if (!prepare_corehost_dir(base, corehost_dir))
        {
            std::printf("  [skip] cannot prepare corehost test directory\n");
            std::printf("  0 passed, 0 failed, 1 skipped\n");
            return 0;
        }

        DWORD exit_code = 0;
        if (!run_in_dir(corehost_dir, L"api", exit_code))
        {
            std::printf("  FAILED: corehost api scenario exit=%lu\n", static_cast<unsigned long>(exit_code));
            return 1;
        }

        const auto values = read_json(corehost_dir / L"result.json");
        print_values("  corehost", values);
        const bool ok = values.count("in_mode") != 0 && values.count("out_mode") != 0 &&
                        values.find("title") != values.end() && values.at("title") == "diff-title-42" &&
                        values.find("has_window") != values.end() && values.at("has_window") == "1";
        std::printf("  %s\n", ok ? "PASSED" : "FAILED");
        return ok ? 0 : 1;
    }

    std::printf("=== ConPTY Differential (corehost vs conhost) ===\n");
    std::printf("  comparison: enabled (COREHOST_TEST_DIFFERENTIAL=1)\n");
    std::printf("  raw/record Ctrl+C comparison: %s\n",
                compare_ctrl ? "enabled (COREHOST_TEST_DIFFERENTIAL_CTRL=1)" : "disabled");

    std::filesystem::path corehost_dir;
    std::filesystem::path conhost_dir;
    if (!prepare_corehost_dir(base, corehost_dir) || !prepare_conhost_dir(base, conhost_dir))
    {
        std::printf("  [skip] cannot prepare host directories or system conhost.exe missing\n");
        std::printf("  0 passed, 0 failed, 1 skipped\n");
        return 0;
    }

    struct scenario
    {
        const wchar_t *name;
        const char *keys[8];
        size_t key_count;
    };
    // api 场景比较稳定字段；raw/input 只记录，不比较（conhost 在 ConPTY 下
    // 对这两类读不返回，是 conhost 自身问题），且默认不运行。
    const scenario api_scenario{
        L"api", {"in_mode", "out_mode", "attr_after", "cursor_size", "cursor_visible", "title", "title_len"}, 7};
    const scenario ctrl_scenarios[] = {{L"raw_ctrl", {}, 0}, {L"input_ctrl", {}, 0}};

    int mismatches = 0;
    auto run_scenario_pair = [&](const scenario &item) {
        DWORD corehost_exit = 0;
        DWORD conhost_exit = 0;
        if (!run_in_dir(corehost_dir, item.name, corehost_exit) || !run_in_dir(conhost_dir, item.name, conhost_exit))
        {
            std::printf("  FAILED: scenario %ls run failed (corehost exit=%lu conhost exit=%lu)\n", item.name,
                        static_cast<unsigned long>(corehost_exit), static_cast<unsigned long>(conhost_exit));
            ++mismatches;
            return;
        }

        const auto corehost_values = read_json(corehost_dir / L"result.json");
        const auto conhost_values = read_json(conhost_dir / L"result.json");
        if (corehost_values.empty() || conhost_values.empty())
        {
            std::printf("  FAILED: scenario %ls missing result.json\n", item.name);
            ++mismatches;
            return;
        }

        std::printf("  [%ls]\n", item.name);
        print_values("  corehost", corehost_values);
        print_values("  conhost ", conhost_values);

        for (size_t index = 0; index < item.key_count; ++index)
        {
            const char *key = item.keys[index];
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
    };

    run_scenario_pair(api_scenario);
    if (compare_ctrl)
    {
        for (const auto &item : ctrl_scenarios)
            run_scenario_pair(item);
    }

    std::printf("  %s\n", mismatches == 0 ? "PASSED" : "FAILED");
    return mismatches == 0 ? 0 : 1;
}