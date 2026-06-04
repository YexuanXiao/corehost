#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <string_view>
#include "win32/handle.hpp"
#include "utility/notification.hpp"

namespace env
{
namespace detail
{

[[nodiscard]] inline std::wstring file_uri_from_path(win32::wcstring_view path) noexcept
{
    constexpr auto prefix = std::wstring_view{L"file:///"};
    std::wstring uri;
    uri.reserve(prefix.size() + path.size());
    uri.append(prefix);
    for (wchar_t ch : path)
    {
        if (ch == L'\\')
            uri.push_back(L'/');
        else
            uri.push_back(ch);
    }
    return uri;
}

} // namespace detail

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

inline void show_elevated_notification(win32::wcstring_view report_path) noexcept
{
    if (!report_path.empty())
    {
        const auto report_uri = detail::file_uri_from_path(report_path);
        const std::array actions{
            notification::action{L"Open report", report_uri},
        };
        notification::send(L"Execution blocked by security policy",
                           L"CoreHost cannot start the default terminal for an elevated process. A diagnostic report "
                           L"was written to the temporary directory.",
                           actions);
        return;
    }

    notification::send(L"Execution blocked by security policy",
                       L"CoreHost cannot start the default terminal for an elevated process. Please start the terminal "
                       L"as an administrator and then run the program.");
}

inline void show_not_found_notification(win32::wcstring_view report_path) noexcept
{
    constexpr notification::action store_action{
        L"Install Terminal",
        L"ms-windows-store://pdp/?ProductId=9N0DX20HK701",
    };

    if (!report_path.empty())
    {
        const auto report_uri = detail::file_uri_from_path(report_path);
        const std::array actions{
            store_action,
            notification::action{L"Open report", report_uri},
        };
        notification::send(L"No terminal available",
                           L"No default terminal application was found. A diagnostic report was written to the "
                           L"temporary directory.",
                           actions);
        return;
    }

    notification::send(L"No terminal available",
                       L"No default terminal application was found. Install Windows Terminal to restore default "
                       L"terminal support.",
                       std::span{&store_action, 1});
}
} // namespace env
