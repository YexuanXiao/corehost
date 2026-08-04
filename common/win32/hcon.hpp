#pragma once

// RAII wrapper for HPCON (pseudo console handle).
//
// HPCON is closed via ClosePseudoConsole (available on Windows 10 October 2018
// Update and later). This wrapper ensures the handle is always released, even
// when exceptions unwind.

#include <Windows.h>
#include <cassert>
#include <utility>

namespace win32
{
class hcon
{
  public:
    hcon() noexcept = default;

    explicit hcon(HPCON value) noexcept : value_(value)
    {
    }

    ~hcon() noexcept
    {
        clear();
    }

    hcon(const hcon &) = delete;
    hcon &operator=(const hcon &) = delete;

    hcon(hcon &&other) noexcept
    {
        *this = std::move(other);
    }

    hcon &operator=(hcon &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            std::swap(other.value_, value_);
        }
        return *this;
    }

    [[nodiscard]] HPCON get() const noexcept
    {
        return value_;
    }

    [[nodiscard]] HPCON *put() noexcept
    {
        assert(!value_);
        return &value_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return value_ != nullptr;
    }

    void *release() noexcept
    {
        auto temp = value_;
        value_ = nullptr;
        return temp;
    }

    void clear() noexcept
    {
        if (value_ != nullptr)
        {
            ::ClosePseudoConsole(value_);
        }
        value_ = nullptr;
    }

  private:
    HPCON value_{nullptr};
};
} // namespace win32
