// tests/test_conpty_idle_cpu.cpp
// 空闲 CPU 回归测试：验证 I/O 模型不再轮询。
//
// 做法：创建一个没有任何客户端的 ConPTY 会话（corehost 空闲等待 CONNECT），
// 采样 corehost 进程 3 秒内的 CPU 增量。轮询实现约每 16ms 唤醒一次并做两次
// Peek + 一次 IOCTL，3 秒会消耗几十到上百毫秒 CPU；事件驱动实现应接近 0。
#include "test_common.hpp"
#include "conpty_test_helpers.hpp"

#include <tlhelp32.h>

#include <cstdio>

using namespace corehost::test;

namespace
{

// 找到本测试进程直接创建的 corehost.exe（ConPTY 宿主）。
DWORD find_corehost_child()
{
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    const DWORD parent = ::GetCurrentProcessId();
    DWORD found = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok != FALSE; ok = ::Process32NextW(snapshot, &entry))
    {
        if (entry.th32ParentProcessID == parent && ::_wcsicmp(entry.szExeFile, L"corehost.exe") == 0)
            found = entry.th32ProcessID;
    }
    ::CloseHandle(snapshot);
    return found;
}

// 返回进程累计 CPU 时间（100ns 单位）。失败返回 0。
ULONGLONG process_cpu_100ns(DWORD pid)
{
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
        return 0;

    FILETIME creation{}, exit_time{}, kernel{}, user{};
    ULONGLONG result = 0;
    if (::GetProcessTimes(process, &creation, &exit_time, &kernel, &user) != FALSE)
    {
        ULARGE_INTEGER k{};
        ULARGE_INTEGER u{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        result = k.QuadPart + u.QuadPart;
    }
    ::CloseHandle(process);
    return result;
}

bool idle_does_not_burn_cpu()
{
    conpty_instance ci;
    ASSERT(create_conpty(ci, {80, 25}));

    // 等待宿主进程出现（CreateProcess 是同步的，这里只是容忍调度延迟）。
    DWORD pid = 0;
    for (int attempt = 0; attempt < 100 && pid == 0; ++attempt)
    {
        pid = find_corehost_child();
        if (pid == 0)
            sleep_ms(20);
    }
    ASSERT(pid != 0);

    // 让启动初始化（日志、初始 VT 序列）结束再开始采样。
    sleep_ms(300);

    const auto before = process_cpu_100ns(pid);
    ASSERT(before != 0);

    sleep_ms(3000);

    const auto after = process_cpu_100ns(pid);
    ASSERT(after >= before);

    const auto delta_ms = (after - before) / 10000ULL;
    std::wcout << L"    idle cpu: " << delta_ms << L" ms over 3000 ms" << std::endl;

    // 阈值 100ms/3s ≈ 3% 单核。轮询实现会明显超过；事件驱动实现接近 0。
    ASSERT(delta_ms < 100ULL);
    return true;
}

} // namespace

int main()
{
    std::wcout << L"=== ConPTY Idle CPU ===" << std::endl;
    RUN_TEST(idle_does_not_burn_cpu, L"Idle CPU without input");
    std::wcout << L"  " << tests_passed << L" passed, " << tests_failed << L" failed, " << (tests_passed + tests_failed)
               << L" total." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
