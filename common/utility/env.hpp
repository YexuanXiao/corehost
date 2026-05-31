#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include "win32/handle.hpp"
#include "utility/notification.hpp"

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

inline void show_elevated_notification(std::wstring_view image_path)
{
    std::wstring_view intro =
        L"CoreHost cannot start the default terminal for an elevated process. Start the terminal as administrator, "
        L"then run ";
    std::wstring_view suffix = L" again.";

    std::wstring body;
    body.reserve(intro.size() + image_path.size() + suffix.size());
    body.append(intro);
    body.append(image_path);
    body.append(suffix);

    notification::send(L"Execution blocked by security policy", body);
}

inline void show_not_found_notification()
{
    constexpr notification::action store_action{
        L"Open Microsoft Store",
        L"ms-windows-store://pdp/?ProductId=9N0DX20HK701",
    };
    notification::send(L"No terminal available",
                       L"No default terminal application was found. Install Windows Terminal to restore default "
                       L"terminal support.",
                       &store_action);
}
} // namespace env
