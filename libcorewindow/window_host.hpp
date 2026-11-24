#pragma once

#include "console.hpp"
#include "render_worker.hpp"
#include "win32/error.hpp"
#include "win32/srwlock.hpp"
#include "win32/string.hpp"

#include <Windows.h>

#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace corehost::window
{

enum class client_exit_policy : std::uint8_t
{
    keep_open,
    exit_if_never_read_input,
    always_exit,
};

inline constexpr UINT client_exit_message = WM_APP + 2;

// Top-level Win32 host. It owns only the window and input; all rendering and
// console damage sampling happen on render_worker's thread.
class window_host final
{
  public:
    window_host(win32::wcstring_view title, console &console, win32::srwlock &console_mutex,
                win32::handle_view client_process, client_exit_policy policy = client_exit_policy::keep_open,
                console_palette palette = console_palette::campbell())
        : title_{title}, console_{console}, console_mutex_{console_mutex}, client_process_{client_process},
          client_policy_{policy}, worker_{console, console_mutex, palette}
    {
        assert(!title_.empty());
        assert(client_process_.valid());
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

        start_process_monitor(window);
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
    static DWORD WINAPI process_thread_entry(LPVOID self)
    {
        static_cast<window_host *>(self)->monitor_process();
        return 0;
    }

    void start_process_monitor(HWND window)
    {
        window_ = window;
        process_thread_ =
            win32::basic_thread{&window_host::process_thread_entry, this, {.name = L"corehost-window-process"}};
    }

    void stop_process_monitor()
    {
        if (!process_thread_.valid())
        {
            return;
        }
        process_stop_event_.set();
        process_thread_.join();
        process_thread_ = {};
    }

    void monitor_process()
    {
        HANDLE waits[] = {client_process_.get(), process_stop_event_.get()};
        const auto wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0)
        {
            return;
        }

        bool close_window = client_policy_ == client_exit_policy::always_exit;
        if (client_policy_ == client_exit_policy::exit_if_never_read_input)
        {
            std::unique_lock<win32::srwlock> lock{console_mutex_};
            close_window = !console_.has_read_input();
        }

        (void)::PostMessageW(window_, close_window ? WM_CLOSE : client_exit_message, 0, 0);
    }

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
        case WM_SETFOCUS:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.set_focused(true);
            }
            return 0;
        case WM_KILLFOCUS:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.set_focused(false);
            }
            return 0;
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
        case client_exit_message:
            if (self != nullptr)
            {
                std::wstring status{self->title_.data(), self->title_.size()};
                status += L" (client exited)";
                ::SetWindowTextW(hwnd, status.c_str());
            }
            return 0;
        case WM_CLOSE:
            if (self != nullptr)
            {
                self->stop_process_monitor();
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
                self->stop_process_monitor();
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
    win32::handle_view client_process_;
    client_exit_policy client_policy_{};
    win32::event process_stop_event_{win32::create_tag, true, false};
    win32::basic_thread process_thread_;
    HWND window_{};
    render_worker worker_;
};

} // namespace corehost::window
