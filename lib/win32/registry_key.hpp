#pragma once

// RAII wrapper for registry keys opened with RegOpenKeyExW / RegCreateKeyExW.
//
// Automatically calls RegCloseKey on destruction. Move-only.
//
// Usage:
//   auto key = win32::registry_key{win32::open_tag, win32::predefined_key::hkcu, L"Console"};
//   auto sub = win32::registry_key{win32::open_tag, key, L"SubKey"};
//   auto new_key = win32::registry_key{win32::create_tag, win32::predefined_key::hklm, L"Software\\MyApp"};

#include "win32/error.hpp"
#include "win32/string.hpp"
#include "win32/tags.hpp"
#include <Windows.h>
#include <cassert>
#include <cstddef>

namespace win32
{

enum class predefined_key : std::uintptr_t
{
    null = 0,
    hkcr = 0x80000000,
    hkcu = 0x80000001,
    hklm = 0x80000002,
    hku = 0x80000003,
    /*
    hkpd = 0x80000004,
    hkpt = 0x80000050,
    hkpnlst = 0x80000060,
    hkcc = 0x80000005,
    hkdd = 0x80000006,   // removed since windows 2000
    hkculs = 0x80000007,
    */
};

// Convert predefined_key to HKEY.
[[nodiscard]] inline HKEY to_hkey(predefined_key key) noexcept
{
    return reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(key));
}

class registry_key
{
  public:
    registry_key() noexcept = default;

    // ── Adopt existing raw HKEY (takes ownership, will RegCloseKey) ──
    explicit registry_key(HKEY raw) noexcept : value_(raw)
    {
    }

    // ── Open existing key (from predefined_key) ────────────────
    registry_key(open_tag_t /*tag*/, predefined_key parent, win32::wcstring_view sub_key,
                 REGSAM desired_access = KEY_READ, DWORD options = 0)
    {
        auto result = ::RegOpenKeyExW(to_hkey(parent), sub_key.c_str(), options, desired_access, &value_);
        if (result != ERROR_SUCCESS)
        {
            throw static_cast<win32::error>(result);
        }
    }

    // ── Open existing key (from registry_key const&) ───────────
    registry_key(open_tag_t /*tag*/, registry_key const &parent, win32::wcstring_view sub_key,
                 REGSAM desired_access = KEY_READ, DWORD options = 0)
    {
        auto result = ::RegOpenKeyExW(parent.get(), sub_key.c_str(), options, desired_access, &value_);
        if (result != ERROR_SUCCESS)
        {
            throw static_cast<win32::error>(result);
        }
    }

    // ── Create/open key (from predefined_key) ──────────────────
    registry_key(create_tag_t /*tag*/, predefined_key parent, win32::wcstring_view sub_key,
                 REGSAM desired_access = KEY_ALL_ACCESS, DWORD options = REG_OPTION_NON_VOLATILE,
                 const wchar_t *class_name = nullptr, LPSECURITY_ATTRIBUTES security_attributes = nullptr,
                 LPDWORD disposition = nullptr)
    {
        DWORD disp{};
        auto result = ::RegCreateKeyExW(to_hkey(parent), sub_key.c_str(), 0, const_cast<wchar_t *>(class_name), options,
                                        desired_access, security_attributes, &value_, &disp);
        if (result != ERROR_SUCCESS)
        {
            throw static_cast<win32::error>(result);
        }
        if (disposition != nullptr)
        {
            *disposition = disp;
        }
    }

    // ── Create/open key (from registry_key const&) ────────────
    registry_key(create_tag_t /*tag*/, registry_key const &parent, win32::wcstring_view sub_key,
                 REGSAM desired_access = KEY_ALL_ACCESS, DWORD options = REG_OPTION_NON_VOLATILE,
                 const wchar_t *class_name = nullptr, LPSECURITY_ATTRIBUTES security_attributes = nullptr,
                 LPDWORD disposition = nullptr)
    {
        DWORD disp{};
        auto result = ::RegCreateKeyExW(parent.get(), sub_key.c_str(), 0, const_cast<wchar_t *>(class_name), options,
                                        desired_access, security_attributes, &value_, &disp);
        if (result != ERROR_SUCCESS)
        {
            throw static_cast<win32::error>(result);
        }
        if (disposition != nullptr)
        {
            *disposition = disp;
        }
    }

    ~registry_key() noexcept
    {
        clear();
    }

    registry_key(const registry_key &) = delete;
    registry_key &operator=(const registry_key &) = delete;

    registry_key(registry_key &&other) noexcept
    {
        *this = std::move(other);
    }

    registry_key &operator=(registry_key &&other) noexcept
    {
        if (this != &other)
        {
            clear();
            std::swap(other.value_, value_);
        }
        return *this;
    }

    [[nodiscard]] HKEY get() const noexcept
    {
        return value_;
    }

    [[nodiscard]] HKEY *put() noexcept
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
            auto result = ::RegCloseKey(value_);
            assert(result == ERROR_SUCCESS);
            (void)result;
        }
        value_ = nullptr;
    }

  private:
    HKEY value_{nullptr};
};
} // namespace win32
