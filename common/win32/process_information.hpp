// ── win32/process_information.hpp ────────────────────────────
// PROCESS_INFORMATION RAII 包装
//
// 成员 layout 与 PROCESS_INFORMATION 逐字段匹配,
// 可通过 reinterpret_cast<PPROCESS_INFORMATION>(&pi) 直接传入
// CreateProcess / CreateProcessAsUser。
//
// 析构时自动 CloseHandle(hThread) 和 CloseHandle(hProcess)。

#pragma once
#include "win32/handle.hpp"

namespace win32
{

struct process_information
{
    win32::handle h_process{};
    win32::handle h_thread{};
    DWORD process_id = 0;
    DWORD thread_id = 0;
};

static_assert(sizeof(::PROCESS_INFORMATION) == sizeof(process_information));

} // namespace win32
