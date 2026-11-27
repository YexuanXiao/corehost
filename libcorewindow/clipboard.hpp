#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>

namespace corehost::window
{
class clipboard_lock final
{
  public:
    explicit clipboard_lock(HWND owner) noexcept : opened_{::OpenClipboard(owner) != FALSE} {}
    ~clipboard_lock()
    {
        if (opened_)
        {
            ::CloseClipboard();
        }
    }

    clipboard_lock(const clipboard_lock &) = delete;
    clipboard_lock &operator=(const clipboard_lock &) = delete;

    [[nodiscard]] bool opened() const noexcept { return opened_; }

  private:
    bool opened_{};
};

class clipboard_text final
{
  public:
    clipboard_text() = default;
    explicit clipboard_text(HANDLE data) noexcept : data_{data}
    {
        if (data_ != nullptr)
        {
            text_ = static_cast<const char16_t *>(::GlobalLock(data_));
            if (text_ != nullptr)
            {
                const auto bytes = ::GlobalSize(data_);
                length_ = bytes / sizeof(char16_t);
                while (length_ > 0 && text_[length_ - 1] == 0)
                {
                    --length_;
                }
            }
        }
    }

    ~clipboard_text()
    {
        if (text_ != nullptr)
        {
            ::GlobalUnlock(data_);
        }
    }

    clipboard_text(const clipboard_text &) = delete;
    clipboard_text &operator=(const clipboard_text &) = delete;
    clipboard_text(clipboard_text &&other) noexcept
        : data_{other.data_}, text_{other.text_}, length_{other.length_}
    {
        other.text_ = nullptr;
        other.data_ = nullptr;
        other.length_ = 0;
    }
    clipboard_text &operator=(clipboard_text &&) = delete;

    [[nodiscard]] bool valid() const noexcept { return text_ != nullptr; }
    [[nodiscard]] std::u16string_view view() const noexcept { return {text_, length_}; }

  private:
    HANDLE data_{};
    const char16_t *text_{};
    std::size_t length_{};
};

class clipboard final
{
  public:
    explicit clipboard(HWND owner) noexcept : lock_{owner} {}

    clipboard(const clipboard &) = delete;
    clipboard &operator=(const clipboard &) = delete;

    [[nodiscard]] bool opened() const noexcept { return lock_.opened(); }

    [[nodiscard]] std::optional<clipboard_text> read_text() const
    {
        if (!opened())
        {
            return std::nullopt;
        }
        return clipboard_text{::GetClipboardData(CF_UNICODETEXT)};
    }

    [[nodiscard]] bool write_text(std::u16string_view text) const
    {
        if (!opened())
        {
            return false;
        }
        const auto bytes = (text.size() + 1) * sizeof(char16_t);
        HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr)
        {
            return false;
        }
        auto *destination = static_cast<char16_t *>(::GlobalLock(memory));
        if (destination == nullptr)
        {
            ::GlobalFree(memory);
            return false;
        }
        std::memcpy(destination, text.data(), text.size() * sizeof(char16_t));
        destination[text.size()] = 0;
        ::GlobalUnlock(memory);
        if (::EmptyClipboard() == FALSE || ::SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
        {
            ::GlobalFree(memory);
            return false;
        }
        return true;
    }

  private:
    clipboard_lock lock_;
};
} // namespace corehost::window