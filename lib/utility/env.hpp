#pragma once
#include <windows.h>
#include <format>
#include <limits>
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

inline void show_elevated_notification(DWORD process_id, DWORD process_group_id, std::wstring_view image_path)
{
    constexpr auto intro = std::wstring_view{L"管理员权限的控制台主机无法把控制权移交给普通权限终端。"};
    constexpr auto process_id_label = std::wstring_view{L"\n进程 PID: "};
    constexpr auto process_group_label = std::wstring_view{L"\n进程组: "};
    constexpr auto image_path_label = std::wstring_view{L"\n程序路径:\n"};
    constexpr auto hint = std::wstring_view{L"\n请在管理员权限的终端中重新运行该程序。"};
    constexpr auto max_dword_digits = std::numeric_limits<DWORD>::digits10 + 1;

    auto body_size_bound = intro.size() + process_id_label.size() + max_dword_digits + process_group_label.size() +
                           max_dword_digits + image_path_label.size() + image_path.size() + hint.size();

    std::wstring body;
    body.resize_and_overwrite(body_size_bound, [&](wchar_t *buffer, size_t) noexcept {
        auto out = std::format_to(buffer, L"{}{}{}{}{}{}{}{}", intro, process_id_label, process_id, process_group_label,
                                  process_group_id, image_path_label, image_path, hint);
        return static_cast<size_t>(out - buffer);
    });

    notification::send(L"请求被安全策略阻止", body);
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
