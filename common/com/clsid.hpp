// ── def/clsid.hpp ────────────────────────────────────────
// 默认终端协议涉及的 COM 常量
//
// 包含 COM 类 CLSID (用于 CoCreateInstance) 和接口 IID
// (用于 QueryInterface / CoCreateInstance 的 riid 参数)。

#pragma once
#include <windows.h>
#include <objbase.h>
#include "win32/registry_key.hpp"
#include "win32/error.hpp"
#include "win32/hresult.hpp"

namespace clsid
{

// ═══════════════════════════════════════════════
//  COM 类 CLSID — 用于 CoCreateInstance
// ═══════════════════════════════════════════════

// ── 零值/旧 conhost ─────────────────────────
inline constexpr CLSID zero{};

// 标准 conhost CLSID — 新 conhost 自身不能作为终端，注册表中指向此 CLSID 视为无效
inline constexpr CLSID conhost{0xb23d10c0, 0xe52e, 0x411e, {0x9d, 0x5b, 0xc0, 0x9f, 0xdf, 0x70, 0x9c, 0x7d}};

// ── Windows Terminal 稳定版 ──────────────────
// IConsoleHandoff 实现
inline constexpr CLSID wt_console{0x2eaca947, 0x7f5f, 0x4cfa, {0xba, 0x87, 0x8f, 0x7f, 0xbe, 0xef, 0xbe, 0x69}};
// ITerminalHandoff 实现 (未使用，预留)
inline constexpr CLSID wt_terminal{0xe12cff52, 0xa866, 0x4c77, {0x9a, 0x90, 0xf5, 0x70, 0xa7, 0xaa, 0x2c, 0x6b}};

// ── Windows Terminal Preview ────────────────
inline constexpr CLSID wt_console_pre{0x06ec847c, 0xc0a5, 0x46b8, {0x92, 0xcb, 0x7c, 0x92, 0xf6, 0xe3, 0x5c, 0xd5}};
inline constexpr CLSID wt_terminal_pre{0x86633f1f, 0x6454, 0x40ec, {0x89, 0xce, 0xda, 0x4e, 0xba, 0x97, 0x7e, 0xe2}};

// ── Windows Terminal Canary ──────────────────
inline constexpr CLSID wt_console_can{0xa854d02a, 0xf2fe, 0x44a5, {0xbb, 0x24, 0xd0, 0x3f, 0x4c, 0xf8, 0x30, 0xd4}};
inline constexpr CLSID wt_terminal_can{0x1706609c, 0xa4ce, 0x4c0d, {0xb7, 0xd2, 0xc1, 0x9b, 0xf6, 0x63, 0x98, 0xa5}};

// ── Windows Terminal Dev ─────────────────────
inline constexpr CLSID wt_console_dev{0x1f9f2bf5, 0x5bc3, 0x4f17, {0xb0, 0xe6, 0x91, 0x24, 0x13, 0xf1, 0xf4, 0x51}};
inline constexpr CLSID wt_terminal_dev{0x051f34ee, 0xc1fd, 0x4b19, {0xaf, 0x75, 0x9b, 0xa5, 0x46, 0x48, 0x43, 0x4c}};

// corehost 自身的 IConsoleHandoff 实现 — 用于 CoRegisterClassObject (-Embedding)
// 注意：此 CLSID 用于自身的 COM 服务器注册，不在默认终端链中。

inline constexpr CLSID corehost_console{0x47a3a1a0, 0x2d3c, 0x4f5e, {0x8b, 0x1a, 0x9c, 0x3d, 0x4e, 0x5f, 0x6a, 0x7b}};

enum class delegation_step
{
    console = 1,
    terminal
};
// 查询注册表中配置的默认终端 CLSID
inline CLSID default_clsid(delegation_step step)
{
    auto name = step == delegation_step::console ? L"DelegationConsole" : L"DelegationTerminal";
    win32::registry_key key;
    try
    {
        key = win32::registry_key{win32::open_tag, win32::predefined_key::hkcu, L"Console\\%%Startup"};
    }
    catch (...)
    {
        return clsid::zero;
    }
    constexpr size_t CLSID_STR_MAX = 39; // 38 字符 + 1 空终止符

    wchar_t buf[CLSID_STR_MAX] = {};
    DWORD size = sizeof(buf);

    auto rc = ::RegGetValueW(key.get(), nullptr, name,
                             RRF_RT_REG_SZ, // 只读取 REG_SZ，类型不匹配则失败
                             nullptr, buf, &size);
    if (win32::error(rc) != win32::error::success)
        return clsid::zero;

    CLSID clsid;

    if (win32::failed(static_cast<win32::hresult>(::CLSIDFromString(buf, &clsid))))
        return clsid::zero;

    return clsid;
}
} // namespace clsid

namespace iid
{
// ═══════════════════════════════════════════════
//  COM 接口 IID — 用于 QueryInterface / riid
// ═══════════════════════════════════════════════

// IConsoleHandoff — 默认终端移交的主接口
// 方法: EstablishHandoff(server, inputEvent, msg, signalPipe, inboxProcess, &clientProcess)
inline constexpr IID iid_console_handoff{0xe686c757, 0x9a35, 0x4a1c, {0xb3, 0xce, 0x0b, 0xcc, 0x8b, 0x5c, 0x69, 0xf4}};

// IDefaultTerminalMarker — 空标记接口，声明"我是默认终端"
// 无任何方法，仅继承 IUnknown
inline constexpr IID iid_default_terminal_marker{
    0x746e6bc0, 0xab05, 0x4e38, {0xab, 0x14, 0x71, 0xe8, 0x67, 0x63, 0x14, 0x1f}};

// ITerminalHandoff3 — 第二个跃点：新 conhost → Windows Terminal
// 方法: EstablishPtyHandoff(&in, &out, signal, reference, server, client, &startupInfo)
inline constexpr IID iid_terminal_handoff3{
    0x6f23da90, 0x15c5, 0x4203, {0x9d, 0xb0, 0x64, 0xe7, 0x3f, 0x1b, 0x1b, 0x00}};

} // namespace iid
