#pragma once

#include "console.hpp"
#include "utility/log.hpp"
#include "render_worker.hpp"
#include "tsf_host.hpp"
#include "win32/error.hpp"
#include "win32/srwlock.hpp"
#include "scrollbar.hpp"
#include "win32/string.hpp"

#include <Windows.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
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
    static constexpr std::size_t initial_columns = 120;
    static constexpr std::size_t initial_rows = 30;
    static constexpr float font_size_dip = 16.0f;

    // The console must have the same grid size as the first window frame;
    // otherwise the first frame resizes it and discards output that a TUI has
    // already painted (notably the alternate screen used by vim/edit).
    [[nodiscard]] static std::pair<std::size_t, std::size_t> initial_cell_size() noexcept
    {
        return {initial_columns, initial_rows};
    }

    window_host(win32::wcstring_view title, console &console, win32::srwlock &console_mutex,
                win32::handle_view client_process, client_exit_policy policy = client_exit_policy::keep_open,
                console_palette palette = console_palette::campbell(),
                uitext::text_options text_options = {})
        : title_{title}, console_{console}, console_mutex_{console_mutex}, client_process_{client_process},
          client_policy_{policy}, worker_{console, console_mutex, palette, text_options},
          window_{create_window(text_options)},
          tsf_{window_, tsf_host::callbacks{
                            .context = this,
                            .commit = &window_host::tsf_commit_callback,
                            .composition = &window_host::tsf_composition_callback,
                            .caret = &window_host::tsf_caret_callback,
                        }}
    {
        assert(!title_.empty());
        assert(client_process_.valid());
    }

    window_host(const window_host &) = delete;
    window_host &operator=(const window_host &) = delete;

    [[nodiscard]] HWND window() const noexcept
    {
        return window_;
    }

    [[nodiscard]] int run()
    {
        start_process_monitor(window_);
        worker_.start(window_);
        ::ShowWindow(window_, SW_SHOWDEFAULT);
        ::UpdateWindow(window_);

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
    [[nodiscard]] static std::pair<int, int> initial_client_size(UINT dpi, uitext::font_family font)
    {
        const auto families = uitext::default_font_families(font);
        const auto scale = static_cast<float>(dpi) / 96.0f;
        uitext::text_engine text{families, font_size_dip * scale};
        const auto metrics = text.metrics();
        const auto row_height = metrics.ascent + metrics.descent + metrics.line_gap;
        return {std::max(1, static_cast<int>(std::ceil(metrics.advance * static_cast<float>(initial_columns)))),
                std::max(1, static_cast<int>(std::ceil(row_height * static_cast<float>(initial_rows))))};
    }

    [[nodiscard]] HWND create_window(uitext::text_options text_options)
    {
        const auto instance = ::GetModuleHandleW(nullptr);
        constexpr const wchar_t *class_name = L"CoreHostWindow";

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &window_host::window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name;
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.hIcon =
            reinterpret_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                                                 LR_DEFAULTSIZE | LR_SHARED));
        window_class.hIconSm = reinterpret_cast<HICON>(
            ::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON),
                         ::GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (::RegisterClassExW(&window_class) == 0)
        {
            const auto error = win32::get_last_error();
            if (error != win32::error::class_already_exists)
            {
                win32::throw_last_error(error);
            }
        }

        const auto initial_dpi = std::max<UINT>(96, ::GetDpiForSystem());
        const auto [client_width, client_height] = initial_client_size(initial_dpi, text_options.font);
        RECT initial_rect{0, 0, client_width, client_height};
        if (::AdjustWindowRectExForDpi(&initial_rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                       initial_dpi) == FALSE)
        {
            win32::throw_last_error();
        }
        const auto initial_width = initial_rect.right - initial_rect.left;
        const auto initial_height = initial_rect.bottom - initial_rect.top;

        const auto window = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, class_name, title_.c_str(),
                                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, initial_width,
                                              initial_height, nullptr, nullptr, instance, this);
        if (window == nullptr)
        {
            win32::throw_last_error();
        }

        const auto window_dpi = std::max<UINT>(96, ::GetDpiForWindow(window));
        if (window_dpi != initial_dpi)
        {
            const auto [target_width, target_height] = initial_client_size(window_dpi, text_options.font);
            RECT target_rect{0, 0, target_width, target_height};
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
        return window;
    }

    static void tsf_commit_callback(void *context, std::wstring_view text) noexcept
    {
        static_cast<window_host *>(context)->handle_tsf_commit(text);
    }

    static void tsf_composition_callback(void *context, std::wstring_view text) noexcept
    {
        static_cast<window_host *>(context)->handle_tsf_composition(text);
    }

    static RECT tsf_caret_callback(void *context) noexcept
    {
        return static_cast<window_host *>(context)->tsf_caret_rect();
    }

    void handle_tsf_commit(std::wstring_view text) noexcept
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        console_.clear_selection();
        console_.set_ime_composition({});
        for (const auto character : text)
        {
            console_.push_input(consoleinput::input_event{
                .type = consoleinput::input_event_type::key,
                .key = consoleinput::key_from_text_character(character),
            });
        }
        worker_.request_redraw();
    }

    void handle_tsf_composition(std::wstring_view text) noexcept
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        console_.set_ime_composition(
            std::u16string_view{reinterpret_cast<const char16_t *>(text.data()), text.size()});
        LOG2("tsf composition length=%zu", text.size());
        worker_.request_redraw();
    }

    [[nodiscard]] RECT tsf_caret_rect() const noexcept
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        POINT origin{0, 0};
        ::ClientToScreen(window_, &origin);
        const auto cursor = console_.state().cursor();
        RECT rect{};
        rect.left = origin.x + static_cast<LONG>(static_cast<float>(cursor.x) * console_.cell_width());
        rect.top = origin.y + static_cast<LONG>(static_cast<float>(cursor.y) * console_.cell_height());
        rect.right = rect.left + static_cast<LONG>(console_.cell_width());
        rect.bottom = rect.top + static_cast<LONG>(console_.cell_height());
        return rect;
    }

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

    [[nodiscard]] std::pair<std::size_t, std::size_t> cell_at(HWND hwnd, LPARAM lparam) const noexcept
    {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        return {console_.cell_column(static_cast<float>(point.x), console_.buffer().width() - 1),
                console_.cell_row(static_cast<float>(point.y), console_.buffer().height() - 1)};
    }

    [[nodiscard]] std::pair<std::size_t, std::size_t> view_cell_at(HWND hwnd, LPARAM lparam) const
    {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const auto display_row = console_.cell_row(static_cast<float>(point.y), console_.buffer().height() - 1);
        const auto history = console_.history_size();
        const auto offset_rows = console_.scroll_pixels() / static_cast<double>(console_.cell_height());
        const auto top = static_cast<double>(history) - offset_rows;
        const auto virtual_row = static_cast<std::size_t>(std::max(0.0, top + static_cast<double>(display_row)));
        return {console_.cell_column(static_cast<float>(point.x), console_.buffer().width() - 1),
                std::min(virtual_row, console_.virtual_rows() - 1)};
    }

    void begin_selection(HWND hwnd, LPARAM lparam)
    {
        auto [x, y] = view_cell_at(hwnd, lparam);
        x = console_.snap_selection_column(x, y);
        const auto now = ::GetTickCount64();
        const bool same_cell = x == last_click_x_ && y == last_click_y_;
        click_count_ = same_cell && now - last_click_time_ <= ::GetDoubleClickTime()
                           ? std::min(click_count_ + 1, 3)
                           : 1;
        last_click_time_ = now;
        last_click_x_ = x;
        last_click_y_ = y;

        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (click_count_ >= 3)
        {
            console_.select_line(y);
            selection_anchor_x_ = 0;
            selection_anchor_y_ = y;
        }
        else if (click_count_ == 2)
        {
            console_.select_word(x, y);
            selection_anchor_x_ = console_.selection().anchor_x;
            selection_anchor_y_ = console_.selection().anchor_y;
        }
        else if (shift && console_.has_selection())
        {
            const auto &selection = console_.selection();
            selection_anchor_x_ = selection.anchor_x;
            selection_anchor_y_ = selection.anchor_y;
            console_.set_selection(selection_anchor_x_, selection_anchor_y_, x, y);
        }
        else
        {
            selection_anchor_x_ = x;
            selection_anchor_y_ = y;
            console_.set_selection(x, y, x, y);
        }
        selection_dragging_ = click_count_ == 1 || shift;
        ::SetCapture(hwnd);
    }

    void update_selection(HWND hwnd, LPARAM lparam)
    {
        if (!selection_dragging_)
        {
            return;
        }
        auto [x, y] = view_cell_at(hwnd, lparam);
        x = console_.snap_selection_column(x, y);
        console_.set_selection(selection_anchor_x_, selection_anchor_y_, x, y);
    }

    [[nodiscard]] bool mouse_tracking() const noexcept
    {
        return console_.mouse_reporting();
    }

    void push_mouse(LPARAM lparam, std::uint32_t button_state, std::uint32_t event_flags)
    {
        const auto [x, y] = cell_at(window_, lparam);
        consoleinput::input_event event{
            .type = consoleinput::input_event_type::mouse,
            .mouse = consoleinput::mouse_event{.x = static_cast<std::int32_t>(x),
                                               .y = static_cast<std::int32_t>(y),
                                               .button_state = button_state,
                                               .control_state = control_state(),
                                               .event_flags = event_flags},
        };
        console_.push_input(std::move(event));
        LOG2("push_mouse queued=%zu", console_.input().record_count());
        worker_.request_redraw();
    }

    void copy_selection(HWND hwnd)
    {
        auto text = console_.selected_text();
        if (text.empty() || ::OpenClipboard(hwnd) == FALSE)
        {
            return;
        }
        ::EmptyClipboard();
        const auto bytes = (text.size() + 1) * sizeof(char16_t);
        if (auto *memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr)
        {
            if (auto *destination = static_cast<char16_t *>(::GlobalLock(memory)); destination != nullptr)
            {
                std::copy(text.begin(), text.end(), destination);
                destination[text.size()] = 0;
                ::GlobalUnlock(memory);
                if (::SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
                {
                    ::GlobalFree(memory);
                }
            }
            else
            {
                ::GlobalFree(memory);
            }
        }
        ::CloseClipboard();
    }

    void paste_clipboard(HWND hwnd)
    {
        console_.clear_selection();
        if (::OpenClipboard(hwnd) == FALSE)
        {
            return;
        }
        if (auto *data = ::GetClipboardData(CF_UNICODETEXT); data != nullptr)
        {
            if (auto *text = static_cast<const char16_t *>(::GlobalLock(data)); text != nullptr)
            {
                bool previous_was_cr = false;
                for (const auto *cursor = text; *cursor != 0; ++cursor)
                {
                    auto character = *cursor;
                    if (character == u'\n')
                    {
                        if (previous_was_cr)
                        {
                            previous_was_cr = false;
                            continue;
                        }
                        character = u'\r';
                    }
                    previous_was_cr = character == u'\r';
                    console_.push_input(consoleinput::input_event{
                        .type = consoleinput::input_event_type::key,
                        .key = consoleinput::key_from_text_character(character),
                    });
                }
                ::GlobalUnlock(data);
            }
        }
        ::CloseClipboard();
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
        return scrollbar::compute(width - bar_width, 0.0f, bar_width, height, state,
                                   scrollbar::track_inset * scale);
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
                const auto row_height = static_cast<double>(console_.scroll_row_height());
                console_.scroll_by_pixels(desired * row_height - console_.scroll_pixels());
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
        const auto row_height = static_cast<double>(console_.scroll_row_height());
        console_.scroll_by_pixels(desired * row_height - console_.scroll_pixels());
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
                lock.unlock();
                self->tsf_.focus();
            }
            return 0;
        case WM_KILLFOCUS:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                self->console_.set_focused(false);
                lock.unlock();
                self->tsf_.unfocus();
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (self != nullptr)
            {
                const auto delta = GET_WHEEL_DELTA_WPARAM(wparam);
                if (delta != 0)
                {
                    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    if (!shift && self->console_.mouse_reporting())
                    {
                        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                        ::ScreenToClient(hwnd, &point);
                        const auto packed = static_cast<LPARAM>((point.y << 16) | (point.x & 0xFFFF));
                        self->push_mouse(packed, static_cast<std::uint32_t>(static_cast<std::uint16_t>(delta)) << 16,
                                         MOUSE_WHEELED);
                    }
                    else
                    {
                        self->console_.scroll_by(static_cast<double>(delta) * 3.0 /
                                                 static_cast<double>(WHEEL_DELTA));
                    }
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self != nullptr)
            {
                self->update_scrollbar_hover(hwnd, lparam);
                if (self->scrollbar_dragging_)
                {
                    self->handle_scrollbar_mouse_move(hwnd, lparam);
                    return 0;
                }
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->selection_dragging_)
                {
                    self->update_selection(hwnd, lparam);
                }
                else if (self->console_.mouse_reporting())
                {
                    self->push_mouse(lparam, (::GetKeyState(VK_LBUTTON) & 0x8000) != 0
                                                   ? FROM_LEFT_1ST_BUTTON_PRESSED
                                                   : 0,
                                       MOUSE_MOVED);
                }
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
                self->worker_.request_atlas_upload();
                const auto x = static_cast<float>(GET_X_LPARAM(lparam));
                const auto y = static_cast<float>(GET_Y_LPARAM(lparam));
                bool over_scrollbar = false;
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    const auto geometry = self->scrollbar_geometry(hwnd);
                    over_scrollbar =
                        scrollbar::hit_test(geometry, x, y) != scrollbar::hit_target::none;
                }
                if (over_scrollbar)
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.set_scrollbar_hovered(true);
                    lock.unlock();
                    self->handle_scrollbar_mouse_down(hwnd, lparam);
                    return 0;
                }

                const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (!shift && self->console_.mouse_reporting())
                {
                    self->push_mouse(lparam, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
                    return 0;
                }
                self->begin_selection(hwnd, lparam);
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->console_.vt_mouse_tracking())
                {
                    self->push_mouse(lparam, RIGHTMOST_BUTTON_PRESSED, 0);
                }
            }
            return 0;
        case WM_RBUTTONUP:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->console_.vt_mouse_tracking())
                {
                    self->push_mouse(lparam, 0, 0);
                }
                else if (self->console_.has_selection())
                {
                    self->copy_selection(hwnd);
                    self->console_.clear_selection();
                    self->worker_.request_redraw();
                }
                else
                {
                    self->paste_clipboard(hwnd);
                }
            }
            return 0;
        case WM_MBUTTONDOWN:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->console_.mouse_reporting())
                {
                    self->push_mouse(lparam, FROM_LEFT_2ND_BUTTON_PRESSED, 0);
                }
            }
            return 0;
        case WM_MBUTTONUP:
            if (self != nullptr)
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->console_.mouse_reporting())
                {
                    self->push_mouse(lparam, 0, 0);
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (self != nullptr)
            {
                if (self->selection_dragging_)
                {
                    self->selection_dragging_ = false;
                    ::ReleaseCapture();
                    return 0;
                }
                if (self->scrollbar_dragging_)
                {
                    self->scrollbar_dragging_ = false;
                    ::ReleaseCapture();
                    return 0;
                }
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                if (self->console_.mouse_reporting())
                {
                    self->push_mouse(lparam, 0, 0);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (self != nullptr)
            {
                const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (control && shift && (wparam == 'C' || wparam == 'V'))
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    if (wparam == 'C')
                    {
                        self->copy_selection(hwnd);
                    }
                    else
                    {
                        self->paste_clipboard(hwnd);
                    }
                    return 0;
                }
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
                    self->console_.clear_selection();
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
                if ((character == 0x03 || character == 0x16 || character == 0x13) &&
                    (::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
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
                    self->console_.clear_selection();
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
    render_worker worker_;
    HWND window_;
    tsf_host tsf_;
    client_exit_policy client_policy_{};
    win32::event process_stop_event_{win32::create_tag, true, false};
    win32::basic_thread process_thread_;
    bool scrollbar_dragging_{};
    bool selection_dragging_{};
    std::size_t selection_anchor_x_{};
    std::size_t selection_anchor_y_{};
    std::size_t last_click_x_{};
    std::size_t last_click_y_{};
    ULONGLONG last_click_time_{};
    int click_count_{};
    bool tracking_mouse_leave_{};
    float scrollbar_grab_offset_{};
};

} // namespace corehost::window
