#pragma once

// ── Utility helpers replacing WIL macros ──────────────────────
//
// 提供简洁、具体的 RAII 类型和帮助函数替代 WIL 宏。
//
// 使用场景：
//   - log_if_failed(hr) : 非致命 HRESULT 失败，记录但不中断
//   - fail_fast_if_null(ptr) : 空指针时快速失败（abort）
//   - fail_fast_if(cond) : 条件满足时快速失败

#include <cassert>
#include <cstdlib>

#include <Windows.h>
#include <winerror.h>

namespace utility
{

// ── log_if_failed ─────────────────────────────────────────────
//
// 替换 WIL 的 LOG_IF_FAILED。调用 HRESULT 表达式并检查结果。
// 如果失败，输出到 debug 并跟踪，但不改变控制流。
// 如果表达式成功，返回 true；失败返回 false。

// ── log_if_failed (HRESULT 直接传入) ─────────────────────────
// 如果已经得到 HRESULT 值，直接传入检查。
inline void log_if_failed(HRESULT hr) noexcept
{
    if (FAILED(hr))
    {
        ::OutputDebugStringW(L"log_if_failed: HRESULT 0x");
        wchar_t buf[16];
        swprintf_s(buf, L"%08lX", static_cast<unsigned long>(hr));
        ::OutputDebugStringW(buf);
        ::OutputDebugStringW(L"\n");
    }
}

// ── log_if_failed (带消息) ──────────────────────────────────
// 替换 WIL 的 LOG_HR_MSG。记录错误码和附加消息文本。
inline void log_if_failed(HRESULT hr, const wchar_t *message) noexcept
{
    if (FAILED(hr))
    {
        ::OutputDebugStringW(L"log_if_failed: HRESULT 0x");
        wchar_t buf[16];
        swprintf_s(buf, L"%08lX", static_cast<unsigned long>(hr));
        ::OutputDebugStringW(buf);
        ::OutputDebugStringW(L" — ");
        ::OutputDebugStringW(message ? message : L"");
        ::OutputDebugStringW(L"\n");
    }
}

// ── fail_fast_if_null ─────────────────────────────────────────
//
// 替换 WIL 的 FAIL_FAST_IF_NULL。
// 如果指针为空，以 HRESULT 信息 abort。
template <typename T>
void fail_fast_if_null(T *ptr, HRESULT hr = E_POINTER) noexcept
{
    if (ptr == nullptr)
    {
        ::OutputDebugStringW(L"fail_fast_if_null: null pointer, HRESULT 0x");
        wchar_t buf[16];
        swprintf_s(buf, L"%08lX", static_cast<unsigned long>(hr));
        ::OutputDebugStringW(buf);
        ::OutputDebugStringW(L"\n");
        std::abort();
    }
}

// ── fail_fast_if ─────────────────────────────────────────────
//
// 替换 WIL 的 FAIL_FAST_IF / FAIL_FAST_IF_MSG。
// 如果条件为真，快速失败。
inline void fail_fast_if(bool cond, HRESULT hr = E_UNEXPECTED, const wchar_t *message = nullptr) noexcept
{
    if (static_cast<bool>(cond))
    {
        ::OutputDebugStringW(L"fail_fast_if: condition true, HRESULT 0x");
        wchar_t buf[16];
        swprintf_s(buf, L"%08lX", static_cast<unsigned long>(hr));
        ::OutputDebugStringW(buf);
        if (message)
        {
            ::OutputDebugStringW(L" — ");
            ::OutputDebugStringW(message);
        }
        ::OutputDebugStringW(L"\n");
        std::abort();
    }
}

} // namespace utility
