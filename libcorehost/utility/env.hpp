#pragma once
#include <windows.h>
#include <shellapi.h>
#include "win32/handle.hpp"

namespace env
{

// ──────────────────────────────────────────────────────────
// UAC 提升检测
//
// 当控制台程序通过 UAC 提升 (sudo / runas) 启动时：
//   - conhost 进程主令牌为 High IL (管理员完整性级别)
//   - Windows 同时维护一个"链接令牌" (Linked Token)，即提升前
//     原始用户的 Medium IL 令牌
//
// 双重检查：TokenElevation（主令牌是否提升）+ TokenLinkedToken
// （链接令牌是否存在）。两者同时成立才返回 true。
// ──────────────────────────────────────────────────────────
[[nodiscard]] inline bool is_elevated() noexcept
{
    win32::handle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, reinterpret_cast<PHANDLE>(token.put())))
        return false;

    TOKEN_ELEVATION elev{};
    DWORD sz = sizeof(elev);
    if (!::GetTokenInformation(token.get(), TokenElevation, &elev, sz, &sz) || !elev.TokenIsElevated)
        return false;

    HANDLE linked_raw = nullptr;
    if (!::GetTokenInformation(token.get(), TokenLinkedToken, &linked_raw, sizeof(linked_raw), &sz))
        return false;

    if (!::CloseHandle(linked_raw))
        return false;

    return true;
}

inline void show_elevated_message()
{
    (void)::MessageBoxW(nullptr,
                        L"当前控制台程序以管理员权限运行，并且需要一个终端。\n"
                        L"Windows 的安全策略阻止具有管理员的控制台主机移交控制"
                        L"台到第三方终端。\n"
                        L"请先以管理员身份运行终端，再运行控制台程序，如果已经"
                        L"在管理员权限的终端中，请直接运行控制台程序，不需要重"
                        L"新提升权限。\n",
                        L"请求被安全策略阻止", MB_OK | MB_ICONINFORMATION);
}
inline void show_not_found_message()
{
    auto rc = ::MessageBoxW(nullptr,
                            L"未找到默认终端应用程序。\n"
                            L"请安装 Windows Terminal 以恢复系统功能。\n"
                            L"选择\"是\"将前往 Microsoft Store。",
                            L"无可用 Windows 终端", MB_YESNO | MB_ICONWARNING);
    if (rc == IDYES)
    {
        (void)::ShellExecuteW(nullptr, L"open", L"ms-windows-store://pdp/?ProductId=9N0DX20HK701", nullptr, nullptr,
                              SW_SHOWNORMAL);
    }
}
} // namespace env