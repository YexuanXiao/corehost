#pragma once

#include <windows.h>

#include <cassert>

#include "ntapi/conwinuserrefs.h"

// user32!ConsoleControl 的进程内解析。调用方只在初始化/创建会话时解析，
// 后续直接使用函数指针；不做 LoadLibrary 回退，因为 user32 在控制台进程里
// 必然已经加载。
using PFN_ConsoleControl = NTSTATUS(WINAPI *)(CONSOLECONTROL Command, PVOID ConsoleInformation,
                                              DWORD ConsoleInformationLength);

[[nodiscard]] inline PFN_ConsoleControl load_console_control() noexcept
{
    HMODULE user32 = nullptr;
    const auto result =
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"user32.dll", &user32);
    if (result == 0 || user32 == nullptr)
        user32 = ::LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(user32 != nullptr);

    const auto function = reinterpret_cast<PFN_ConsoleControl>(::GetProcAddress(user32, "ConsoleControl"));
    assert(function != nullptr);
    return function;
}
