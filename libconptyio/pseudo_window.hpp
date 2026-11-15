#pragma once

#include <windows.h>

namespace corehost::conptyio
{

// Message-only window used by headless sessions to keep ConDrv/window APIs
// well-formed without creating a visible console window.
class pseudo_window final
{
  public:
    pseudo_window() = default;

    pseudo_window(const pseudo_window &) = delete;
    pseudo_window &operator=(const pseudo_window &) = delete;

    ~pseudo_window()
    {
        if (window_ != nullptr)
        {
            ::DestroyWindow(window_);
        }
        if (class_registered_)
        {
            ::UnregisterClassW(class_name_, ::GetModuleHandleW(nullptr));
        }
    }

    [[nodiscard]] bool create() noexcept
    {
        if (window_ != nullptr)
        {
            return true;
        }

        const auto instance = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = ::DefWindowProcW;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name_;
        if (::RegisterClassExW(&window_class) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
        class_registered_ = true;

        window_ = ::CreateWindowExW(0, class_name_, L"corehost2", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, instance, nullptr);
        return window_ != nullptr;
    }

    void show(bool visible) noexcept
    {
        if (window_ != nullptr)
        {
            ::ShowWindow(window_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }

    void set_parent(HWND parent) noexcept
    {
        if (window_ != nullptr)
        {
            ::SetParent(window_, parent);
        }
    }

    [[nodiscard]] HWND handle() const noexcept
    {
        return window_;
    }

  private:
    static constexpr wchar_t class_name_[] = L"Corehost2PseudoWindow";
    HWND window_{};
    bool class_registered_{};
};

} // namespace corehost::conptyio