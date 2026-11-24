#pragma once

#include "console.hpp"
#include "render_worker.hpp"
#include "win32/error.hpp"
#include "win32/srwlock.hpp"
#include "win32/string.hpp"

#include <Windows.h>

#include <cassert>
#include <mutex>
#include <utility>

namespace corehost::window
{

// Top-level Win32 host. It owns only the window and input; all rendering and
// console damage sampling happen on render_worker's thread.
class window_host final
{
  public:
    window_host(win32::wcstring_view title, console &console, win32::srwlock &console_mutex)
        : title_{title}, console_{console}, console_mutex_{console_mutex}, worker_{console, console_mutex}
    {
        assert(!title_.empty());
    }

    window_host(const window_host &) = delete;
    window_host &operator=(const window_host &) = delete;

    [[nodiscard]] int run()
    {
        const auto instance = ::GetModuleHandleW(nullptr);
        constexpr const wchar_t *class_name = L"CoreHostWindow";

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &window_host::window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name;
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        if (::RegisterClassExW(&window_class) == 0)
        {
            const auto error = win32::get_last_error();
            if (error != win32::error::class_already_exists)
            {
                win32::throw_last_error(error);
            }
        }

        const HWND window =
            ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, class_name, title_.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, 1200, 720, nullptr, nullptr, instance, this);
        if (window == nullptr)
        {
            win32::throw_last_error();
        }

        worker_.start(window);
        ::ShowWindow(window, SW_SHOWDEFAULT);
        ::UpdateWindow(window);

        MSG message{};
        for (;;)
        {
            const auto result = ::GetMessageW(&message, nullptr, 0, 0);
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

        worker_.stop();
        return static_cast<int>(message.wParam);
    }

  private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto *self = reinterpret_cast<window_host *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize = POINT{320, 200};
            return 0;
        case WM_DPICHANGED: {
            const auto *suggested = reinterpret_cast<const RECT *>(lparam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                           suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CHAR:
            if (self != nullptr)
            {
                const auto character = static_cast<char16_t>(wparam);
                if (character >= 0x20 || character == u'\r' || character == u'\n' || character == u'\b' ||
                    character == u'\t')
                {
                    auto event = consoleinput::input_event{
                        .type = consoleinput::input_event_type::key,
                        .key = consoleinput::key_from_character(character),
                    };
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.push_input(std::move(event));
                }
            }
            return 0;
        case WM_CLOSE:
            if (self != nullptr)
            {
                self->worker_.stop();
            }
            ::DestroyWindow(hwnd);
            return 0;
        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (self != nullptr)
            {
                self->worker_.stop();
            }
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    win32::wcstring_view title_;
    console &console_;
    win32::srwlock &console_mutex_;
    render_worker worker_;
};

} // namespace corehost::window
