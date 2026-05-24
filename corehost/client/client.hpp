// ── client/client_entry.hpp ──────────────────────────────────
// 客户端启动模式入口
//
//   corehost <cmd>
//     1. CreateProcess(cmd, CREATE_NEW_CONSOLE)
//        └─ConDrv 为新进程创建控制台
//          ConDrv 启动 conhost.exe 0x<HANDLE> (defterm 路径)
//          defterm 实例 → IO 循环 → COM 移交 → WT
//
//     3. corehost 退出
//

#pragma once
#include <windows.h>
#include <string>
#include "win32/error.hpp"
#include "win32/process_information.hpp"

namespace client
{

inline void client_entry(std::wstring client_command_line)
{
    STARTUPINFOW si{sizeof(si)};
    // 返回后 pi 析构 → CloseHandle(hThread) + CloseHandle(hProcess) → 进程退出
    win32::process_information pi{};
    // lpApplicationName=NULL, lpCommandLine=full command line
    // (CreateProcessW may modify lpCommandLine, so pass .data() of mutable wstring)
    if (!::CreateProcessW(nullptr, client_command_line.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                          nullptr, &si, reinterpret_cast<::PPROCESS_INFORMATION>(&pi)))
        win32::throw_last_error();
}

} // namespace client
