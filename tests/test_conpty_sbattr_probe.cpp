// === tests/test_conpty_sbattr_probe.cpp (v2) ===
// 探针：完整模拟 powershell 5.1 错误消息渲染的 API 序列（含 GetConsoleCP
// 轮询 / GetSBInfo 多次 / SetConsoleMode），分别在 corehost 与系统 conhost
// 下运行，对比 GetConsoleScreenBufferInfo 返回值与最终文本颜色。
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "libconpty/libconpty.hpp"
#include "win32/handle.hpp"
#include "win32/wait.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace
{

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

[[noreturn]] void fail_win32(const char *message)
{
    std::fprintf(stderr, "FAIL: %s: %lu\n", message, ::GetLastError());
    std::exit(1);
}

static WORD read_sb_attr(const char *tag)
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info))
        fail_win32("GetConsoleScreenBufferInfo");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[probe] %s attrs=0x%04X cursor=(%d,%d)\n", tag,
                  static_cast<unsigned>(info.wAttributes), static_cast<int>(info.dwCursorPosition.X),
                  static_cast<int>(info.dwCursorPosition.Y));
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), buf, static_cast<DWORD>(std::strlen(buf)), &written, nullptr);
    return info.wAttributes;
}

static void set_attr(WORD attr)
{
    ::SetConsoleTextAttribute(::GetStdHandle(STD_OUTPUT_HANDLE), attr);
}

// 子进程模式：模拟 powershell 5.1 错误消息渲染
int child_mode()
{
    const HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);

    // 阶段 1：错误消息开始前的准备（对应 powershell 5.1 序列）
    set_attr(0x0007);
    read_sb_attr("A1-after-attr07");

    set_attr(0x0007);
    // 模式检查
    DWORD mode = 0;
    ::GetConsoleMode(hOut, &mode);
    ::SetConsoleMode(hOut, 0x0007); // processed|wrap|VT

    // GetConsoleCP 轮询（powershell 5.1 表现出的轮询行为）
    for (int i = 0; i < 50; ++i)
        ::GetConsoleCP();
    for (int i = 0; i < 6; ++i)
        read_sb_attr("B-poll-sb");
    for (int i = 0; i < 50; ++i)
        ::GetConsoleCP();
    read_sb_attr("C-after-poll");
    ::GetConsoleMode(hOut, &mode);
    for (int i = 0; i < 3; ++i)
        read_sb_attr("D-pre-attr0c");

    // 阶段 2：颜色设置（对应 powershell 5.1 的 SetAttr(0C) 序列）
    set_attr(0x000C);
    const WORD a2 = read_sb_attr("A2-after-attr0c");
    ::GetConsoleMode(hOut, &mode);

    // 模拟 powershell 5.1 的"第 4 次 SetAttr"（观察为 R<->B 交换）
    const WORD swapped =
        static_cast<WORD>((a2 & 0x0008) | ((a2 & 0x0004) >> 2) | ((a2 & 0x0002) >> 0) | ((a2 & 0x0001) << 2));
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[probe] A2=0x%04X swapped=0x%04X\n", static_cast<unsigned>(a2),
                  static_cast<unsigned>(swapped));
    DWORD written = 0;
    ::WriteFile(hOut, buf, static_cast<DWORD>(std::strlen(buf)), &written, nullptr);
    set_attr(swapped);

    // 阶段 3：写错误文本（WriteConsoleW 模拟）
    const wchar_t text[] = L"hello : probe error message";
    ::WriteConsoleW(hOut, text, static_cast<DWORD>(std::wcslen(text)), &written, nullptr);

    // 阶段 4：行尾恢复 + 换行
    set_attr(0x0007);
    set_attr(0x0007);
    ::WriteConsoleW(hOut, L"\n", 1, &written, nullptr);
    return 0;
}

// 父进程模式：创建 ConPTY，启动子进程（本 exe --child），捕获输出。
int parent_mode()
{
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    win32::handle pty_input_read;
    win32::handle input_write;
    win32::handle output_read;
    win32::handle pty_output_write;
    if (!::CreatePipe(pty_input_read.put(), input_write.put(), &inheritable, 0))
        fail_win32("CreatePipe(input) failed");
    if (!::CreatePipe(output_read.put(), pty_output_write.put(), &inheritable, 0))
        fail_win32("CreatePipe(output) failed");

    constexpr COORD size{80, 25};
    HPCON hpc = nullptr;
    const HRESULT hr = ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(), 0, &hpc);
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FAIL: ConptyCreatePseudoConsole hr=0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    pty_input_read.clear();
    pty_output_write.clear();

    console::proc_thread_attribute_list attributes;
    if (FAILED(attributes.initialize(1)))
        fail("InitializeProcThreadAttributeList failed");
    if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc),
                                     nullptr, nullptr))
        fail_win32("UpdateProcThreadAttribute(PSEUDOCONSOLE) failed");

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes.get();

    std::wstring module_path(MAX_PATH, L'\0');
    const DWORD module_len = ::GetModuleFileNameW(nullptr, module_path.data(), MAX_PATH);
    if (module_len == 0 || module_len >= MAX_PATH)
        fail_win32("GetModuleFileNameW");
    module_path.resize(module_len);

    std::wstring command = L"\"" + module_path + L"\" --child";
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                          &startup.StartupInfo, &pi))
        fail_win32("CreateProcessW(child) failed");

    win32::handle process{pi.hProcess};
    ::CloseHandle(pi.hThread);
    ConptyReleasePseudoConsole(hpc);

    // 收集子进程输出（直到进程退出）
    std::string captured;
    std::thread reader([&] {
        char buf[512];
        for (;;)
        {
            DWORD bytes = 0;
            if (!::ReadFile(output_read.get(), buf, sizeof(buf), &bytes, nullptr) || bytes == 0)
                break;
            captured.append(buf, bytes);
        }
    });

    ::WaitForSingleObject(process.get(), 10000);
    DWORD exit_code = 0;
    ::GetExitCodeProcess(process.get(), &exit_code);
    output_read.clear();
    reader.join();

    std::fprintf(stdout, "=== captured %zu bytes ===\n", captured.size());
    for (unsigned char ch : captured)
    {
        if (ch >= 0x20 && ch < 0x7f)
            std::fputc(ch, stdout);
        else
            std::fprintf(stdout, "\\x%02x", ch);
    }
    std::fputc('\n', stdout);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0)
        return child_mode();
    return parent_mode();
}
