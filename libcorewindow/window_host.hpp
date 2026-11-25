#pragma once

#include "console.hpp"
#include "render_worker.hpp"
#include "win32/error.hpp"
#include "win32/srwlock.hpp"
#include "scrollbar.hpp"
#include "win32/string.hpp"

#include <Windows.h>
#include <windowsx.h>

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

        const auto initial_dpi = std::max<UINT>(96, ::GetDpiForSystem());
        RECT initial_rect{0, 0, ::MulDiv(1200, static_cast<int>(initial_dpi), 96),
                          ::MulDiv(720, static_cast<int>(initial_dpi), 96)};
        if (::AdjustWindowRectExForDpi(&initial_rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                       initial_dpi) == FALSE)
        {
            win32::throw_last_error();
        }
        const auto initial_width = initial_rect.right - initial_rect.left;
        const auto initial_height = initial_rect.bottom - initial_rect.top;

        const HWND window = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, class_name, title_.c_str(),
                                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, initial_width,
                                              initial_height, nullptr, nullptr, instance, this);
        if (window == nullptr)
        {
            win32::throw_last_error();
        }

        const auto window_dpi = std::max<UINT>(96, ::GetDpiForWindow(window));
        if (window_dpi != initial_dpi)
        {
            RECT target_rect{0, 0, ::MulDiv(1200, static_cast<int>(window_dpi), 96),
                             ::MulDiv(720, static_cast<int>(window_dpi), 96)};
            if (::AdjustWindowRectExForDpi(&target_rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                           window_dpi) == FALSE)
            {
                win32::throw_last_error();
            }
            if (::SetWindowPos(window, nullptr, 0, 0, target_rect.right - target_rect.left,
                               target_rect.bottom - target_rect.top,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
            {
                win32::throw_last_error();
            }
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
    static std::uint32_t control_state() noexcept
    {
        std::uint32_t state = 0;
        if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            state |= 0x0010;
        }
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            state |= 0x0008;
        }
        if ((::GetKeyState(VK_MENU) & 0x8000) != 0)
        {
            state |= 0x0004;
        }
        return state;
    }

    static consoleinput::key_event character_key(char16_t character) noexcept
    {
        auto event = consoleinput::key_from_character(character);
        event.control_state |= control_state();
        switch (character)
        {
        case u'\r':
        case u'\n':
            event.key = static_cast<consoleinput::virtual_key>(VK_RETURN);
            break;
        case u'\b':
            event.key = static_cast<consoleinput::virtual_key>(VK_BACK);
            break;
        case u'\t':
            event.key = static_cast<consoleinput::virtual_key>(VK_TAB);
            break;
        case 0x1B:
            event.key = static_cast<consoleinput::virtual_key>(VK_ESCAPE);
            break;
        default:
            if (character >= 1 && character <= 26)
            {
                event.key = static_cast<consoleinput::virtual_key>(u'A' + character - 1);
                event.control_state |= 0x0008;
            }
            break;
        }
        return event;
    }

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

    [[nodiscard]] scrollbar::geometry scrollbar_geometry(HWND hwnd) const noexcept
    {
        RECT client{};
        if (::GetClientRect(hwnd, &client) == FALSE)
        {
            return {};
        }
        const auto dpi = std::max<UINT>(96, ::GetDpiForWindow(hwnd));
        const auto scale = static_cast<float>(dpi) / 96.0f;
        const auto width = static_cast<float>(client.right - client.left);
        const auto height = static_cast<float>(client.bottom - client.top);
        const auto rows = console_.buffer().height();
        const auto history = console_.history_size();
        const auto offset = console_.scroll_pixels() / static_cast<double>(console_.scroll_row_height());
        const scrollbar::state state{history + rows, rows, static_cast<double>(history) - offset};
        const auto bar_width = scrollbar::default_width * scale;
        return scrollbar::compute(width - bar_width, 0.0f, bar_width, height, state);
    }

    void update_scrollbar_hover(HWND hwnd, LPARAM lparam)
    {
        const auto x = static_cast<float>(GET_X_LPARAM(lparam));
        const auto y = static_cast<float>(GET_Y_LPARAM(lparam));
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        const auto geometry = scrollbar_geometry(hwnd);
        console_.set_scrollbar_hovered(scrollbar_dragging_ ||
                                       scrollbar::hit_test(geometry, x, y) != scrollbar::hit_target::none);
        if (!tracking_mouse_leave_)
        {
            TRACKMOUSEEVENT track{.cbSize = sizeof(TRACKMOUSEEVENT),
                                  .dwFlags = TME_LEAVE,
                                  .hwndTrack = hwnd,
                                  .dwHoverTime = HOVER_DEFAULT};
            if (::TrackMouseEvent(&track) != FALSE)
            {
                tracking_mouse_leave_ = true;
            }
        }
    }

    void handle_scrollbar_mouse_down(HWND hwnd, LPARAM lparam)
    {
        const auto x = static_cast<float>(GET_X_LPARAM(lparam));
        const auto y = static_cast<float>(GET_Y_LPARAM(lparam));
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        const auto geometry = scrollbar_geometry(hwnd);
        const auto target = scrollbar::hit_test(geometry, x, y);
        const auto rows = console_.buffer().height();
        const auto history = console_.history_size();
        if (target == scrollbar::hit_target::arrow_up)
        {
            console_.scroll_by(1);
        }
        else if (target == scrollbar::hit_target::arrow_down)
        {
            console_.scroll_by(-1);
        }
        else if (target == scrollbar::hit_target::thumb)
        {
            scrollbar_dragging_ = true;
            scrollbar_grab_offset_ = scrollbar::grab_offset(geometry, y);
            ::SetCapture(hwnd);
        }
        else if (target != scrollbar::hit_target::none)
        {
            if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                const scrollbar::state state{history + rows, rows,
                                             static_cast<double>(history) -
                                                 std::min(static_cast<double>(console_.scroll_offset()), static_cast<double>(history))};
                const auto top = scrollbar::top_from_pointer(geometry, y, 0.0f, state);
                const auto desired = static_cast<double>(history) - top;
                console_.scroll_by(desired - console_.scroll_offset());
            }
            else if (target == scrollbar::hit_target::above_thumb)
            {
                console_.scroll_by(static_cast<std::ptrdiff_t>(rows));
            }
            else
            {
                console_.scroll_by(-static_cast<std::ptrdiff_t>(rows));
            }
        }
    }

    void handle_scrollbar_mouse_move(HWND hwnd, LPARAM lparam)
    {
        if (!scrollbar_dragging_)
        {
            return;
        }
        const auto y = static_cast<float>(GET_Y_LPARAM(lparam));
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        const auto geometry = scrollbar_geometry(hwnd);
        const auto rows = console_.buffer().height();
        const auto history = console_.history_size();
        const scrollbar::state state{history + rows, rows,
                                     static_cast<double>(history) -
                                         std::min(static_cast<double>(console_.scroll_offset()), static_cast<double>(history))};
        const auto top = scrollbar::top_from_pointer(geometry, y, scrollbar_grab_offset_, state);
        const auto desired = static_cast<double>(history) - top;
        console_.scroll_by(desired - console_.scroll_offset());
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
        case WM_GETMINMAXINFO: {
            const auto dpi = std::max<UINT>(96, ::GetDpiForWindow(hwnd));
            reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize =
                POINT{::MulDiv(320, static_cast<int>(dpi), 96), ::MulDiv(200, static_cast<int>(dpi), 96)};
            return 0;
        }
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
        case WM_MOUSEWHEEL:
            if (self != nullptr)
            {
                const auto delta = GET_WHEEL_DELTA_WPARAM(wparam);
                if (delta != 0)
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.scroll_by(static_cast<double>(delta) * 3.0 / static_cast<double>(WHEEL_DELTA));
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self != nullptr)
            {
                self->update_scrollbar_hover(hwnd, lparam);
                self->handle_scrollbar_mouse_move(hwnd, lparam);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (self != nullptr)
            {
                self->tracking_mouse_leave_ = false;
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.set_scrollbar_hovered(false);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.set_scrollbar_hovered(true);
            }
            if (self != nullptr)
            {
                self->handle_scrollbar_mouse_down(hwnd, lparam);
            }
            return 0;
        case WM_LBUTTONUP:
            if (self != nullptr && self->scrollbar_dragging_)
            {
                self->scrollbar_dragging_ = false;
                ::ReleaseCapture();
            }
            return 0;
        case WM_KEYDOWN:
            if (self != nullptr)
            {
                const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (wparam == VK_SCROLL || (control && shift && wparam == 'S'))
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.toggle_scroll_lock();
                    return 0;
                }

                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    const auto offset = self->console_.scroll_offset();
                    const auto page = self->console_.buffer().height();
                    std::ptrdiff_t lines = 0;
                    switch (wparam)
                    {
                    case VK_HOME:
                        lines = static_cast<std::ptrdiff_t>(self->console_.history_size());
                        break;
                    case VK_END:
                        lines = -static_cast<std::ptrdiff_t>(self->console_.history_size());
                        break;
                    case VK_PRIOR:
                        lines = static_cast<std::ptrdiff_t>(page);
                        break;
                    case VK_NEXT:
                        lines = -static_cast<std::ptrdiff_t>(page);
                        break;
                    case VK_UP:
                        lines = 1;
                        break;
                    case VK_DOWN:
                        lines = -1;
                        break;
                    default:
                        break;
                    }

                    if (lines != 0 && (offset != 0 || shift))
                    {
                        self->console_.scroll_by(lines);
                        return 0;
                    }
                }

                auto key = consoleinput::virtual_key{};
                bool handled = true;
                switch (wparam)
                {
                case VK_UP:
                    key = consoleinput::virtual_key::up;
                    break;
                case VK_DOWN:
                    key = consoleinput::virtual_key::down;
                    break;
                case VK_LEFT:
                    key = consoleinput::virtual_key::left;
                    break;
                case VK_RIGHT:
                    key = consoleinput::virtual_key::right;
                    break;
                case VK_HOME:
                    key = consoleinput::virtual_key::home;
                    break;
                case VK_END:
                    key = consoleinput::virtual_key::end;
                    break;
                case VK_INSERT:
                    key = consoleinput::virtual_key::insert;
                    break;
                case VK_DELETE:
                    key = consoleinput::virtual_key::del;
                    break;
                case VK_PRIOR:
                    key = consoleinput::virtual_key::page_up;
                    break;
                case VK_NEXT:
                    key = consoleinput::virtual_key::page_down;
                    break;
                case VK_F1:
                case VK_F2:
                case VK_F3:
                case VK_F4:
                case VK_F5:
                case VK_F6:
                case VK_F7:
                case VK_F8:
                case VK_F9:
                case VK_F10:
                case VK_F11:
                case VK_F12:
                    key = static_cast<consoleinput::virtual_key>(wparam);
                    break;
                default:
                    handled = false;
                    break;
                }

                if (handled)
                {
                    auto event = consoleinput::input_event{
                        .type = consoleinput::input_event_type::key,
                        .key =
                            consoleinput::key_event{
                                .character = 0,
                                .key = key,
                                .scan_code = static_cast<std::uint16_t>(LOBYTE(lparam)),
                                .control_state = control_state(),
                            },
                    };
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.snap_to_bottom_if_unlocked();
                    self->console_.push_input(std::move(event));
                    return 0;
                }
            }
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_KEYUP:
            if (self != nullptr)
            {
                auto event = consoleinput::input_event{
                    .type = consoleinput::input_event_type::key,
                    .key =
                        consoleinput::key_event{
                            .character = 0,
                            .key = static_cast<consoleinput::virtual_key>(wparam),
                            .scan_code = static_cast<std::uint16_t>(LOBYTE(lparam)),
                            .control_state = control_state(),
                        },
                };
                event.key.key_down = false;
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.push_input(std::move(event));
            }
            return 0;
        case WM_CHAR:
            if (self != nullptr)
            {
                const auto character = static_cast<char16_t>(wparam);
                if (character == 0x13 && (::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (::GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    return 0;
                }
                if (character >= 0x20 || character == u'\r' || character == u'\n' || character == u'\b' ||
                    character == u'\t' || character == 0x1B || (character >= 1 && character <= 26))
                {
                    auto event = consoleinput::input_event{
                        .type = consoleinput::input_event_type::key,
                        .key = character_key(character),
                    };
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.snap_to_bottom_if_unlocked();
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
    bool scrollbar_dragging_{};
    bool tracking_mouse_leave_{};
    float scrollbar_grab_offset_{};
    render_worker worker_;
};

} // namespace corehost::window
