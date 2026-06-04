#pragma once

#include <windows.h>
#include <cstdint>
#include <cassert>
#include <utility>
#include "error.hpp"
#include "win32/error.hpp"

namespace win32
{
class handle;
class handle_view
{
    friend win32::handle;

  public:
    constexpr handle_view() noexcept = default;

    explicit constexpr handle_view(void *value) noexcept : value_(value)
    {
    }

    constexpr handle_view(const handle_view &other) noexcept : value_(other.get())
    {
    }

    constexpr handle_view &operator=(const handle_view &other) noexcept
    {
        value_ = other.get();
        return *this;
    }

    [[nodiscard]] static handle_view from_uintptr(const std::uintptr_t value) noexcept
    {
        return handle_view(reinterpret_cast<void *>(value));
    }

    [[nodiscard]] constexpr void *get() const noexcept
    {
        return value_;
    }

    [[nodiscard]] std::uintptr_t as_uintptr() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(value_);
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return valid();
    }

    constexpr void clear() noexcept
    {

        value_ = nullptr;
    }

  private:
    void *value_{nullptr};
};

class handle
{
  public:
    handle() noexcept = default;

    explicit handle(void *value) noexcept : value_(value)
    {
    }

    ~handle() noexcept
    {
        clear();
    }

    handle(const handle &) = delete;
    handle &operator=(const handle &) = delete;

    handle(handle &&other) noexcept
    {
        *this = std::move(other);
    }

    handle from_view(handle_view &other) noexcept
    {
        return handle(other.get());
    }

    handle &operator=(handle &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            std::swap(other.value_.value_, value_.value_);
        }
        return *this;
    }

    [[nodiscard]] static handle from_uintptr(const std::uintptr_t value) noexcept
    {
        return handle(reinterpret_cast<void *>(value));
    }

    [[nodiscard]] void *get() const noexcept
    {
        return value_.get();
    }

    [[nodiscard]] handle_view view() const noexcept
    {
        return handle_view(value_);
    }

    [[nodiscard]] void **put() noexcept
    {
        assert(!value_);
        return &value_.value_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return value_.valid();
    }

    void *release() noexcept
    {
        void *temp = value_.get();
        value_.clear();
        return temp;
    }

    void clear() noexcept
    {
        if (value_.valid())
        {
            check_last_error(::CloseHandle(value_.get()) == FALSE);
        }
        value_.clear();
    }

    constexpr operator handle_view() noexcept
    {
        return value_;
    }

  private:
    handle_view value_;
};

// ── DuplicateHandle 帮助函数 ─────────────────────────────

// 复制自身进程伪句柄 (用于传给其他进程)。
// 等价于: DuplicateHandle(GetCurrentProcess, GetCurrentProcess, GetCurrentProcess, ..., SYNCHRONIZE, FALSE, 0)
[[nodiscard]] inline handle duplicate_self(DWORD access = SYNCHRONIZE)
{
    handle h;
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentProcess(), ::GetCurrentProcess(), h.put(), access, FALSE,
                           0))
        win32::throw_last_error();
    return h;
}

// 从外部句柄复制到当前进程。
// dwDesiredAccess=0, dwOptions=DUPLICATE_SAME_ACCESS: 保持与源句柄相同的访问掩码
[[nodiscard]] inline handle duplicate_handle(win32::handle_view src)
{
    handle h;
    if (!::DuplicateHandle(::GetCurrentProcess(), src.get(), ::GetCurrentProcess(), h.put(), 0, FALSE,
                           DUPLICATE_SAME_ACCESS))
        win32::throw_last_error();
    return h;
}

struct pipe
{
    win32::handle read;
    win32::handle write;
};

[[nodiscard]] inline pipe create_pipe()
{
    pipe p;
    if (!::CreatePipe(p.read.put(), p.write.put(), nullptr, 0))
    {
        win32::throw_last_error();
    }
    return p;
}

} // namespace win32
