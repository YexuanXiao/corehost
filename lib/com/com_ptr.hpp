// ── com/com_ptr.hpp ─────────────────────────────────────
// COM 智能指针 + 辅助函数。
//
// 提供:
//   com_ptr<T>            — winrt::com_ptr 别名
//   create_instance<T>()  — CoCreateInstance 包装
//   make<T>()             — 构造 COM 实现对象并包装
//
// 错误统一抛出 win32::hresult。

#pragma once
#include <windows.h>
#include <objbase.h>
#include <utility>
#include <winrt/base.h>
#include "win32/hresult.hpp"

namespace com
{

template <typename T>
using com_ptr = winrt::com_ptr<T>;

// ── create_instance ─────────────────────────────────────
// CoCreateInstance 包装，失败抛 win32::hresult。
template <typename T>
[[nodiscard]] inline com_ptr<T> create_instance(REFCLSID clsid, DWORD cls_context = CLSCTX_LOCAL_SERVER)
{
    com_ptr<T> ptr;
    auto hr = ::CoCreateInstance(clsid, nullptr, cls_context, __uuidof(T), ptr.put_void());
    win32::throw_hresult(win32::hresult(hr));
    return ptr;
}

// ── make ────────────────────────────────────────────────
// 构造 COM 实现对象并包装为 com_ptr。
// 对象初始引用计数为 1 (由 com::implements 构造函数设置)。
template <typename T, typename... Args>
[[nodiscard]] inline com_ptr<T> make(Args &&...args)
{
    com_ptr<T> ptr;
    ptr.attach(new T(std::forward<Args>(args)...));
    return ptr;
}

} // namespace com
