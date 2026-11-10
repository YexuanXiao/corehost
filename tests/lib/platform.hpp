// tests/lib/platform.hpp
// 测试库的宿主环境探测与子进程运行器。
//
// 设计见 doc/design/0003：
// - 前置条件用谓词表达，用例里 SKIP(原因) 跳过，跳过不计为通过；
// - 子进程运行器统一处理 stdout/stderr 捕获、退出码、超时终止；
// - 超时只用于测试基础设施判定失败，不是产品等待上限。
#pragma once
#include <windows.h>
#include <winsvc.h>

#include <string>
#include <cstdio>
#include <string_view>
#include <thread>
#include <vector>

namespace corehost::test
{

// ── 环境探测 ─────────────────────────────────────────────
inline bool file_exists(std::wstring_view path)
{
    const DWORD attributes = ::GetFileAttributesW(std::wstring{path}.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 在 PATH 中查找可执行文件；找不到返回空串。
inline std::wstring find_on_path(std::wstring_view name)
{
    wchar_t buffer[MAX_PATH * 2]{};
    const auto length = ::SearchPathW(nullptr, std::wstring{name}.c_str(), nullptr, ARRAYSIZE(buffer), buffer, nullptr);
    if (length == 0 || length >= ARRAYSIZE(buffer))
        return {};
    return std::wstring{buffer, length};
}

inline std::wstring system32_path(std::wstring_view file_name)
{
    wchar_t system_dir[MAX_PATH]{};
    const UINT length = ::GetSystemDirectoryW(system_dir, ARRAYSIZE(system_dir));
    if (length == 0 || length >= ARRAYSIZE(system_dir))
        return {};
    std::wstring path{system_dir, length};
    path += L"\\";
    path += file_name;
    return path;
}

inline bool has_windows_terminal()
{
    return !find_on_path(L"wt.exe").empty();
}

inline bool has_ssh()
{
    return file_exists(system32_path(L"OpenSSH\\ssh.exe"));
}

inline bool has_sshd()
{
    return file_exists(system32_path(L"OpenSSH\\sshd.exe"));
}

// 查询服务状态；0 表示查询失败或服务不存在。不需要管理员权限。
inline DWORD service_state(std::wstring_view name)
{
    const SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr)
        return 0;

    DWORD state = 0;
    const SC_HANDLE service = ::OpenServiceW(manager, std::wstring{name}.c_str(), SERVICE_QUERY_STATUS);
    if (service != nullptr)
    {
        SERVICE_STATUS status{};
        if (::QueryServiceStatus(service, &status) != FALSE)
            state = status.dwCurrentState;
        ::CloseServiceHandle(service);
    }
    ::CloseServiceHandle(manager);
    return state;
}

// 交互式会话（有可见 window station）才能跑 GUI 相关用例。
inline bool is_interactive_session()
{
    DWORD session_id = 0;
    if (::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id) == FALSE)
        return false;
    if (session_id == 0)
        return false;

    const HWINSTA station = ::GetProcessWindowStation();
    if (station == nullptr)
        return false;

    USEROBJECTFLAGS flags{};
    if (::GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), nullptr) == FALSE)
        return false;
    return (flags.dwFlags & WSF_VISIBLE) != 0;
}

// ── 子进程运行器 ─────────────────────────────────────────
struct process_result
{
    DWORD exit_code = static_cast<DWORD>(-1);
    std::string out;
    std::string err;
    bool timed_out = false;
};

namespace details
{

inline void read_pipe_to_string(HANDLE pipe, std::string &destination)
{
    char buffer[4096];
    for (;;)
    {
        DWORD read = 0;
        if (::ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) == FALSE || read == 0)
            return;
        destination.append(buffer, read);
    }
}

// 创建一个可继承的管道对。
inline bool create_inheritable_pipe(HANDLE &read_end, HANDLE &write_end)
{
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    if (::CreatePipe(&read_end, &write_end, &attributes, 0) == FALSE)
        return false;
    // 读端只留给父进程。
    return ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0) != FALSE;
}

} // namespace details

// 运行命令行并捕获输出。timeout_ms 到点后终止子进程并置 timed_out。
// stdin 接 NUL，避免子进程等待输入导致挂起。
inline process_result run_process(std::wstring_view command_line, DWORD timeout_ms = 30000)
{
    process_result result{};

    HANDLE out_read = nullptr, out_write = nullptr;
    HANDLE err_read = nullptr, err_write = nullptr;
    if (!details::create_inheritable_pipe(out_read, out_write) ||
        !details::create_inheritable_pipe(err_read, err_write))
    {
        result.exit_code = ::GetLastError();
        if (out_read != nullptr)
            ::CloseHandle(out_read);
        if (out_write != nullptr)
            ::CloseHandle(out_write);
        return result;
    }

    const HANDLE nul = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     0, nullptr);

    // 用 Job 对象托管子进程树：超时时 TerminateJobObject 能连孙进程一起结束，
    // 否则 cmd 拉起的子进程会继承管道写端，让读线程一直等到它们自然退出。
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;

    PROCESS_INFORMATION process{};
    std::wstring mutable_command{command_line};

    const BOOL created = ::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                          nullptr, nullptr, &startup, &process);
    if (created == FALSE)
    {
        result.exit_code = ::GetLastError();
        if (nul != INVALID_HANDLE_VALUE)
            ::CloseHandle(nul);
        ::CloseHandle(out_read);
        ::CloseHandle(out_write);
        ::CloseHandle(err_read);
        ::CloseHandle(err_write);
        return result;
    }

    if (job != nullptr && ::AssignProcessToJobObject(job, process.hProcess) == FALSE)
    {
        std::fwprintf(stderr, L"run_process: AssignProcessToJobObject failed: %lu\n", ::GetLastError());
    }

    // 父进程只保留读端；子进程继承的写端在父进程关闭后仍有效。
    ::CloseHandle(out_write);
    ::CloseHandle(err_write);
    if (nul != INVALID_HANDLE_VALUE)
        ::CloseHandle(nul);

    std::thread out_reader{[&] { details::read_pipe_to_string(out_read, result.out); }};
    std::thread err_reader{[&] { details::read_pipe_to_string(err_read, result.err); }};

    const DWORD wait = ::WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait == WAIT_TIMEOUT)
    {
        result.timed_out = true;
        if (job != nullptr)
            ::TerminateJobObject(job, 1);
        else
            ::TerminateProcess(process.hProcess, 1);
        ::WaitForSingleObject(process.hProcess, INFINITE);
    }

    DWORD exit_code = static_cast<DWORD>(-1);
    if (::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE)
        result.exit_code = exit_code;

    // 子进程已退出：管道写端全部关闭，读线程会在 EOF 结束。
    out_reader.join();
    err_reader.join();

    ::CloseHandle(out_read);
    ::CloseHandle(err_read);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    if (job != nullptr)
        ::CloseHandle(job); // KILL_ON_JOB_CLOSE：清掉遗留的孙进程
    return result;
}

// 从 PATH 启动一个标准工具（cmd、powershell 等）。
inline process_result run_tool(std::wstring_view tool, std::wstring_view arguments, DWORD timeout_ms = 30000)
{
    std::wstring command{L"\""};
    command += tool;
    command += L"\" ";
    command += arguments;
    return run_process(command, timeout_ms);
}

} // namespace corehost::test
