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
    std::wstring_view intro = L"控制台主机无法启动默认终端，请先以管理员权限启动终端，再重新运行程序 ";
    std::wstring_view suffix = L"。";

    std::wstring body;
    body.reserve(intro.size() + image_path.size() + suffix.size());
    body.append(intro);
    body.append(image_path);
    body.append(suffix);

    notification::send(L"程序执行被安全策略阻止", body);
}

inline void show_not_found_notification()
{
    constexpr notification::action store_action{
        L"打开 Microsoft Store",
        L"ms-windows-store://pdp/?ProductId=9N0DX20HK701",
    };
    notification::send(L"无可用 Windows 终端", L"未找到默认终端应用程序。请安装 Windows Terminal 以恢复默认终端功能。",
                       &store_action);
}
} // namespace env
