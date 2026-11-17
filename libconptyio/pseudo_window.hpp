#pragma once

#include "raw_buffer.hpp"
#include "win32/error.hpp"
#include "win32/event.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

#include <Windows.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace corehost::conptyio
{

// Message-only pseudo window used by GetConsoleWindow. The window runs on its
// own thread and never touches console state directly: visibility and close
// requests are published through events, and the session loop consumes them.
class pseudo_window final
{
  public:
    explicit pseudo_window(win32::handle_view server = {}, std::wstring_view title = {})
        : server_{server}, title_{title.begin(), title.end()}, ready_{win32::create_tag, true, false},
          close_requested_{win32::create_tag, true, false}, show_event_{win32::create_tag, false, false},
          hide_event_{win32::create_tag, false, false}
    {
    }

    pseudo_window(const pseudo_window &) = delete;
    pseudo_window &operator=(const pseudo_window &) = delete;

    ~pseudo_window()
    {
        if (thread_.valid())
        {
            stop_and_join();
        }
    }

    void start()
    {
        if (thread_.valid())
        {
            return;
        }
        thread_ = win32::basic_thread{&pseudo_window::thread_entry, this, {.name = L"corehost2-window"}};
        ready_.wait();
    }

    void stop_and_join()
    {
        if (!thread_.valid())
        {
            return;
        }
        if (hwnd_ != nullptr && ::IsWindow(hwnd_) != FALSE)
        {
            if (::PostMessageW(hwnd_, wm_stop_thread, 0, 0) == FALSE)
            {
                win32::throw_last_error();
            }
        }
        thread_.join();
    }

    [[nodiscard]] HWND handle() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] HWND hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] win32::handle_view close_requested() const noexcept
    {
        return close_requested_.view();
    }

    [[nodiscard]] win32::handle_view show_event() const noexcept
    {
        return show_event_.view();
    }

    [[nodiscard]] win32::handle_view hide_event() const noexcept
    {
        return hide_event_.view();
    }

    void show(bool visible)
    {
        set_visibility(visible);
    }

    void set_visibility(bool visible)
    {
        if (hwnd_ == nullptr || ::IsWindow(hwnd_) == FALSE)
        {
            return;
        }
        ::SendMessageW(hwnd_, wm_set_visibility, visible ? TRUE : FALSE, 0);
    }

    void set_parent(HWND parent)
    {
        set_owner(parent);
    }

    void set_owner(HWND owner)
    {
        if (hwnd_ == nullptr || ::IsWindow(hwnd_) == FALSE)
        {
            return;
        }
        ::SendMessageW(hwnd_, wm_set_owner, reinterpret_cast<WPARAM>(owner), 0);
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<pseudo_window *>(self)->run();
        return 0;
    }

    void run()
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &pseudo_window::window_proc;
        window_class.hInstance = nullptr;
        window_class.lpszClassName = class_name_;
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        if (::RegisterClassExW(&window_class) == 0)
        {
            ready_.set();
            win32::throw_last_error();
        }

        const HWND created =
            ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, class_name_,
                              title_.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, this);
        if (created == nullptr)
        {
            ready_.set();
            win32::throw_last_error();
        }

        hwnd_ = created;
        ready_.set();

        MSG message{};
        for (;;)
        {
            const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
            if (result == 0)
            {
                break;
            }
            if (result == -1)
            {
                win32::throw_last_error();
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_CREATE)
        {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }

        auto *self = reinterpret_cast<pseudo_window *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr)
        {
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }

        switch (message)
        {
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED)
            {
                self->post_visibility(false);
            }
            else if (wparam == SIZE_RESTORED)
            {
                self->post_visibility(true);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (lparam == 0)
            {
                self->post_visibility(wparam != 0);
            }
            return 0;
        case wm_set_owner:
            ::SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, static_cast<LONG_PTR>(wparam));
            return 0;
        case wm_set_visibility:
            self->suppress_visibility_ = true;
            ::ShowWindow(hwnd, wparam != 0 ? SW_SHOWNOACTIVATE : SW_MINIMIZE);
            self->suppress_visibility_ = false;
            return 0;
        case WM_CLOSE:
            self->close_requested_.set();
            self->cancel_server_io();
            return 0;
        case wm_stop_thread:
            ::PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE: {
            const auto owner = reinterpret_cast<HWND>(::GetWindowLongPtrW(hwnd, GWLP_HWNDPARENT));
            if (owner != nullptr && ::IsWindow(owner) != FALSE)
            {
                ::SetFocus(owner);
            }
            return 0;
        }
        case WM_GETOBJECT:
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    void post_visibility(bool visible) noexcept
    {
        if (suppress_visibility_)
        {
            return;
        }
        if (visible)
        {
            hide_event_.reset();
            show_event_.set();
        }
        else
        {
            show_event_.reset();
            hide_event_.set();
        }
        if (hwnd_ != nullptr)
        {
            cancel_server_io();
        }
    }

    void cancel_server_io() const noexcept
    {
        if (!server_.valid())
        {
            return;
        }
        if (::CancelIoEx(server_.get(), nullptr) != FALSE)
        {
            return;
        }
        if (win32::get_last_error() == win32::error::not_found)
        {
            return;
        }
        std::abort();
    }

    static constexpr UINT wm_stop_thread = WM_APP + 1;
    static constexpr UINT wm_set_visibility = WM_APP + 2;
    static constexpr UINT wm_set_owner = WM_APP + 3;
    static constexpr const wchar_t *class_name_ = L"CorehostPseudoConsoleWindow";

    win32::handle_view server_;
    vtparser::raw_wide_string title_;
    win32::event ready_;
    win32::event close_requested_;
    win32::event show_event_;
    win32::event hide_event_;
    win32::basic_thread thread_;
    HWND hwnd_{};
    bool suppress_visibility_{};
};

} // namespace corehost::conptyio
