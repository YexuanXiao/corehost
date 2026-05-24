// ── com/com_ptr.hpp ─────────────────────────────────────
// COM 智能指针 + 辅助函数 — 替代 winrt::com_ptr。
//
// 提供:
//   com_ptr<T>            — RAII COM 指针 (AddRef/Release)
//   create_instance<T>()  — CoCreateInstance 包装
//   make<T>()             — 构造 COM 实现对象并包装
//
// 错误统一抛出 win32::hresult。

#pragma once
#include <windows.h>
#include <objbase.h>
#include <memory>
#include <type_traits>
#include "win32/hresult.hpp"

namespace com
{

template <typename T>
class com_ptr
{
  public:
    com_ptr() noexcept = default;

    // 接管原始指针 (不 AddRef — 来自 CoCreateInstance / QueryInterface)
    explicit com_ptr(T *p) noexcept : _ptr(p)
    {
    }

    // 接管 std::unique_ptr
    explicit com_ptr(std::unique_ptr<T> p) : _ptr(p.release())
    {
    }

    ~com_ptr()
    {
        clear();
    }

    com_ptr(const com_ptr &other) noexcept : _ptr(other._ptr)
    {
        if (_ptr)
            _ptr->AddRef();
    }

    com_ptr &operator=(const com_ptr &other) noexcept
    {
        if (this != &other)
        {
            clear();
            _ptr = other._ptr;
            if (_ptr)
                _ptr->AddRef();
        }
        return *this;
    }

    com_ptr(com_ptr &&other) noexcept : _ptr(other._ptr)
    {
        other._ptr = nullptr;
    }

    com_ptr &operator=(com_ptr &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    [[nodiscard]] T *get() const noexcept
    {
        return _ptr;
    }

    [[nodiscard]] T *operator->() const noexcept
    {
        return _ptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return _ptr != nullptr;
    }

    // QueryInterface — 失败抛 win32::hresult
    template <typename U>
    [[nodiscard]] com_ptr<U> as() const
    {
        U *raw = nullptr;
        auto hr = _ptr->QueryInterface(__uuidof(U), reinterpret_cast<void **>(&raw));
        win32::throw_hresult(win32::hresult{hr});
        return com_ptr<U>(raw);
    }

    void clear() noexcept
    {
        if (_ptr)
        {
            _ptr->Release();
            _ptr = nullptr;
        }
    }

    void *release()
    {
        void *temp = _ptr;
        _ptr = nullptr;
        return temp;
    }

  private:
    T *_ptr = nullptr;
};

// ── create_instance ─────────────────────────────────────
// CoCreateInstance 包装，失败抛 win32::hresult。
template <typename T>
[[nodiscard]] inline com_ptr<T> create_instance(REFCLSID clsid, DWORD cls_context = CLSCTX_LOCAL_SERVER)
{
    T *raw = nullptr;
    auto hr = ::CoCreateInstance(clsid, nullptr, cls_context, __uuidof(T), reinterpret_cast<void **>(&raw));
    win32::throw_hresult(win32::hresult(hr));
    return com_ptr<T>(raw);
}

// ── make ────────────────────────────────────────────────
// 构造 COM 实现对象并包装为 com_ptr。
// 对象初始引用计数为 1 (由 com::implements 构造函数设置)。
template <typename T, typename... Args>
[[nodiscard]] inline com_ptr<T> make(Args &&...args)
{
    return com_ptr<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

} // namespace com
