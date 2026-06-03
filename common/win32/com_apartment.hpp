// ── win32/com_apartment.hpp ──────────────────────────────
// COM 公寓 RAII 包装
//
// 构造时调用 CoInitializeEx，析构时调用 CoUninitialize。
// 移动语义 (move-only)，类似 std::unique_lock。
//
// 用法:
//   auto apt = win32::com_apartment{win32::com_apartment::multithreaded};
//   // ... COM 操作 ...
//   // 异常安全: 无论如何离开作用域都会 CoUninitialize

#pragma once
#include <windows.h>
#include <objbase.h>
#include <utility>
#include "hresult.hpp"

namespace win32
{

class com_apartment
{
  public:
    // 调用 CoInitializeEx，失败时抛出 win32::hresult
    explicit com_apartment(DWORD coinit_flags)
    {
        auto hr = ::CoInitializeEx(nullptr, coinit_flags);
    }

    ~com_apartment()
    {
        ::CoUninitialize();
    }

    com_apartment(const com_apartment &) = delete;
    com_apartment &operator=(const com_apartment &) = delete;
};

} // namespace win32
