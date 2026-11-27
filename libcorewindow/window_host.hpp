#pragma once

#include "console.hpp"
#include "uia_provider.hpp"
#include "control.hpp"
#include "console_config.hpp"
#include "window_config.hpp"
#include "utility/log.hpp"
#include "render_worker.hpp"
#include "tsf_host.hpp"
#include "win32/error.hpp"
#include "win32/srwlock.hpp"
#include "scrollbar.hpp"
#include "win32/string.hpp"

#include <Windows.h>
#include <dwmapi.h>
#include <shellapi.h>
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



// Top-level Win32 host. It owns only the window and input; all rendering and
// console damage sampling happen on render_worker's thread.
class window_host final
{
  public:

    window_host(win32::wcstring_view title, console &console, win32::srwlock &console_mutex,
                win32::handle_view client_process, const corehost_config &config, win32::handle_view damage_event)
        : title_{title}, console_{console}, console_mutex_{console_mutex}, client_process_{client_process},
          console_config_{config.console}, window_config_{config.window}, clipboard_config_{config.clipboard},
          background_config_{config.background}, color_config_{config.color}, window_behavior_config_{config.window_behavior},
          bell_config_{config.bell}, font_config_{config.font},
          title_config_{config.title}, damage_event_{damage_event},
          worker_{console, console_mutex, window_config_, config.color, config.font, config.bell, config.title, config.accessibility,
                 damage_event_},
          window_{create_window()},
          tsf_{window_, tsf_host::callbacks{
                            .context = this,
                            .commit = &window_host::tsf_commit_callback,
                            .composition = &window_host::tsf_composition_callback,
                            .caret = &window_host::tsf_caret_callback,
                            .candidate_offset_cells = config.window.candidate_offset_cells,
                            .enabled = config.window.tsf_enabled,
                        }}
    {
        assert(!title_.empty());
        assert(client_process_.valid());
        window_ready_ = true;
        worker_.set_text_changed_callback(&window_host::text_changed_notify, this);
    }

    ~window_host() = default;

    window_host(const window_host &) = delete;
    window_host &operator=(const window_host &) = delete;

    [[nodiscard]] HWND window() const noexcept
    {
        return window_;
    }

    static void text_changed_notify(void *context) noexcept
    {
        auto *self = static_cast<window_host *>(context);
        (void)::PostMessageW(self->window_, WM_APP + 0x101, 0, 0);
    }

    IRawElementProviderSimple *ensure_uia_provider() noexcept
    {
        if (!window_config_.uia_enabled)
        {
            return nullptr;
        }
        if (!uia_provider_)
        {
            uia_provider_ = winrt::make<uia_provider>(window_, console_, console_mutex_);
            uia_events_ = uia_provider_.as<IUiaEvents>();
        }
        return uia_provider_.get();
    }

    void notify_uia_selection() noexcept
    {
        if (window_config_.uia_selection_events && uia_events_)
        {
            (void)uia_events_->NotifySelectionChanged();
        }
    }

    void run()
    {
        worker_.start(window_);
        if (window_behavior_config_.launch == launch_mode::fullscreen)
        {
            auto style = static_cast<DWORD>(::GetWindowLongPtrW(window_, GWL_STYLE));
            style &= ~static_cast<DWORD>(WS_OVERLAPPEDWINDOW);
            style |= WS_POPUP;
            ::SetWindowLongPtrW(window_, GWL_STYLE, static_cast<LONG_PTR>(style));
            MONITORINFO monitor_info{sizeof(MONITORINFO)};
            if (::GetMonitorInfoW(::MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY), &monitor_info) != FALSE)
            {
                ::SetWindowPos(window_, HWND_TOP, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                               monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                               monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                               SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            }
        }
        else
        {
            const auto show = window_behavior_config_.launch == launch_mode::maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT;
            ::ShowWindow(window_, show);
        }
        ::UpdateWindow(window_);

        MSG message{};
        for (;;)
        {
            HANDLE handles[]{client_process_.get()};
            const DWORD handle_count = client_exited_ ? 0 : 1;
            const auto wait = ::MsgWaitForMultipleObjectsEx(handle_count, handle_count ? handles : nullptr,
                                                            INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED)
            {
                win32::throw_last_error();
            }
            if (handle_count != 0 && wait == WAIT_OBJECT_0)
            {
                handle_client_exit();
                continue;
            }

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
    }

    void handle_client_exit() noexcept
    {
        if (client_exited_)
        {
            return;
        }
        client_exited_ = true;

        bool close_window = window_config_.exit_policy == client_exit_policy::always_exit;
        if (window_config_.exit_policy == client_exit_policy::exit_if_never_read_input)
        {
            std::unique_lock<win32::srwlock> lock{console_mutex_};
            close_window = !console_.has_read_input();
        }

        if (close_window)
        {
            (void)::PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }

        window_title_buffer_.assign(title_.data(), title_.size());
        window_title_buffer_ += L" (client exited)";
        ::SetWindowTextW(window_, window_title_buffer_.c_str());
    }
  private:
    struct resolved_font_families final
    {
        std::array<uitext::font_fallback_entry, max_font_fallbacks> entries{};
        std::size_t count{};
    };

    [[nodiscard]] static resolved_font_families font_families_from_config(const font_config &config) noexcept
    {
        resolved_font_families result;
        const auto count = std::clamp<std::size_t>(config.family_fallback_list.count, 1, max_font_fallbacks);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &entry = config.family_fallback_list.entries[i];
            result.entries[i] = uitext::font_fallback_entry{
                win32::wcstring_view{entry.family},
                DWRITE_UNICODE_RANGE{entry.range.first, entry.range.last},
            };
        }
        result.count = count;
        return result;
    }

    [[nodiscard]] static std::pair<int, int> initial_client_size(UINT dpi, const font_config &config,
                                                                 std::size_t columns, std::size_t rows)
    {
        const auto families = font_families_from_config(config);
        const auto scale = static_cast<float>(dpi) / 96.0f;
        uitext::text_engine text{std::span<const uitext::font_fallback_entry>{families.entries}.first(families.count),
                                 config.size * scale};
        const auto metrics = text.metrics();
        const auto row_height = metrics.ascent + metrics.descent + metrics.line_gap;
        const auto scrollbar_width = static_cast<int>(std::ceil(scrollbar::default_width * scale));
        return {std::max(1, static_cast<int>(std::ceil(metrics.advance * static_cast<float>(columns))) +
                               scrollbar_width),
                std::max(1, static_cast<int>(std::ceil(row_height * static_cast<float>(rows))))};
    }

    void apply_appearance(HWND window)
    {
        const bool dark_mode = color_config_.mode == theme_mode::dark ||
                               (color_config_.mode == theme_mode::auto_mode && window_config_.dark_mode);
        const BOOL dark = dark_mode ? TRUE : FALSE;
        (void)::DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        if (!window_behavior_config_.shadow)
        {
            const int policy = DWMNCRP_DISABLED;
            (void)::DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        }

        if (window_behavior_config_.blur != backdrop_mode::none)
        {
            const int backdrop = window_behavior_config_.blur == backdrop_mode::mica ? 2 : 3;
            const auto result = ::DwmSetWindowAttribute(window, 38, &backdrop, sizeof(backdrop));
            LOG2("window backdrop mode=%d hr=%08x", backdrop, static_cast<unsigned>(result));
        }

        if (const auto opacity = static_cast<int>(std::lround(std::clamp(background_config_.opacity, 0.0f, 1.0f) * 255.0f)); opacity < 255)
        {
            (void)::SetLayeredWindowAttributes(window, 0, static_cast<BYTE>(opacity), LWA_ALPHA);
        }
    }

    [[nodiscard]] HWND create_window()
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
        const auto [client_width, client_height] =
            initial_client_size(initial_dpi, font_config_, window_config_.initial_columns,
                                window_config_.initial_rows);
        RECT initial_rect{0, 0, client_width, client_height};
        if (::AdjustWindowRectExForDpi(&initial_rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                       initial_dpi) == FALSE)
        {
            win32::throw_last_error();
        }
        const auto initial_width = initial_rect.right - initial_rect.left;
        const auto initial_height = initial_rect.bottom - initial_rect.top;

        auto ex_style = static_cast<DWORD>(WS_EX_NOREDIRECTIONBITMAP);
        if (const auto opacity = static_cast<int>(std::lround(std::clamp(background_config_.opacity, 0.0f, 1.0f) * 255.0f)); opacity < 255)
        {
            ex_style |= WS_EX_LAYERED;
        }
        const auto window = ::CreateWindowExW(ex_style, class_name,
                                              (title_config_.force_corehost ? L"corehost" : title_.c_str()),
                                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, initial_width,
                                              initial_height, nullptr, nullptr, instance, this);
        if (window == nullptr)
        {
            win32::throw_last_error();
        }

        apply_appearance(window);
        ::SetWindowPos(window, window_behavior_config_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const auto window_dpi = std::max<UINT>(96, ::GetDpiForWindow(window));
        if (window_dpi != initial_dpi)
        {
            const auto [target_width, target_height] =
                initial_client_size(window_dpi, font_config_, window_config_.initial_columns,
                                    window_config_.initial_rows);
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

    // Chromium's InputMethodWinBase deliberately keeps handling these IMM32
    // requests even when TSF is the active input path. Some TSF-native TIPs
    // still send WM_IME_REQUEST for reverse conversion and for querying the
    // candidate position, so dropping these messages would break reconversion
    // and IME UI placement. See chromium_ime_src/ui/base/ime/win/
    // input_method_win_base.cc and input_method_win_tsf.cc.
    LRESULT handle_ime_request(WPARAM request, LPARAM data)
    {
        if (window_ == nullptr || !window_ready_)
        {
            return 0;
        }

        switch (request)
        {
        case IMR_RECONVERTSTRING:
            return handle_reconvert_string(reinterpret_cast<RECONVERTSTRING *>(data), false);
        case IMR_DOCUMENTFEED:
            return handle_reconvert_string(reinterpret_cast<RECONVERTSTRING *>(data), true);
        case IMR_QUERYCHARPOSITION:
            return handle_query_char_position(reinterpret_cast<IMECHARPOSITION *>(data));
        default:
            return 0;
        }
    }

    LRESULT handle_query_char_position(IMECHARPOSITION *position) const
    {
        if (position == nullptr || position->dwSize < sizeof(IMECHARPOSITION))
        {
            return 0;
        }

        const auto caret = tsf_caret_rect();
        position->pt = POINT{caret.left, caret.top};
        position->cLineHeight = static_cast<DWORD>(std::max<LONG>(1, caret.bottom - caret.top));
        position->rcDocument = client_rect_screen();
        return 1;
    }

    LRESULT handle_reconvert_string(RECONVERTSTRING *reconvert, bool include_context)
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        auto &text = ime_text_buffer_;
        text.clear();
        std::size_t target_offset = 0;
        std::size_t target_length = 0;

        // If TSF has an active composition, expose that as the conversion
        // target. Otherwise use a local selection, then fall back to the
        // current console row around the caret.
        if (!console_.ime_composition().empty())
        {
            text.assign(console_.ime_composition());
            target_length = text.size();
        }
        else if (console_.selection().active)
        {
            console_.selected_text_into(text);
            target_length = text.size();
        }
        else
        {
            const auto cursor = console_.state().cursor();
            if (cursor.y < console_.buffer().height())
            {
                console_.buffer().line_text_into(static_cast<std::size_t>(cursor.y), text);
            }
            while (!text.empty() && text.back() == u' ')
            {
                text.pop_back();
            }
            target_offset = std::min<std::size_t>(cursor.x, text.size());
            target_length = 0;
        }

        if (text.empty())
        {
            return 0;
        }

        const auto bytes = sizeof(RECONVERTSTRING) + text.size() * sizeof(char16_t);
        if (reconvert == nullptr)
        {
            return static_cast<LRESULT>(bytes);
        }
        if (reconvert->dwSize < bytes)
        {
            return 0;
        }

        reconvert->dwVersion = 0;
        reconvert->dwStrLen = static_cast<DWORD>(text.size());
        reconvert->dwStrOffset = sizeof(RECONVERTSTRING);
        reconvert->dwCompStrLen = include_context ? static_cast<DWORD>(target_length) : 0;
        reconvert->dwCompStrOffset = static_cast<DWORD>(target_offset * sizeof(char16_t));
        reconvert->dwTargetStrLen = static_cast<DWORD>(target_length);
        reconvert->dwTargetStrOffset = reconvert->dwCompStrOffset;

        auto *destination = reinterpret_cast<char *>(reconvert) + sizeof(RECONVERTSTRING);
        std::memcpy(destination, text.data(), text.size() * sizeof(char16_t));
        LOG2("ime request reconvert len=%zu target=%zu context=%d", text.size(), target_length,
             include_context ? 1 : 0);
        return reinterpret_cast<LRESULT>(reconvert);
    }

    [[nodiscard]] RECT client_rect_screen() const
    {
        RECT client{};
        if (window_ != nullptr && ::GetClientRect(window_, &client))
        {
            POINT origin{0, 0};
            ::ClientToScreen(window_, &origin);
            client.left += origin.x;
            client.right += origin.x;
            client.top += origin.y;
            client.bottom += origin.y;
        }
        return client;
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

    static constexpr UINT_PTR selection_scroll_timer = 1;

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
        selection_drag_point_ = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ::SetCapture(hwnd);
        ::SetTimer(hwnd, selection_scroll_timer, window_config_.selection_auto_scroll_interval_ms, nullptr);
        notify_uia_selection();
    }

    void update_selection(HWND hwnd, LPARAM lparam)
    {
        if (!selection_dragging_)
        {
            return;
        }
        selection_drag_point_ = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        auto [x, y] = view_cell_at(hwnd, lparam);
        x = console_.snap_selection_column(x, y);
        console_.set_selection(selection_anchor_x_, selection_anchor_y_, x, y);
        notify_uia_selection();
    }

    void update_hyperlink_hover(HWND hwnd, LPARAM lparam)
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        const auto [x, y] = view_cell_at(hwnd, lparam);
        const auto link = console_.cell_at_virtual(y, x).hyperlink();
        if (link != console_.hovered_hyperlink())
        {
            LOG2("hyperlink hover x=%zu y=%zu link=%u", x, y, static_cast<unsigned>(link));
            console_.set_hovered_hyperlink(link);
            worker_.request_redraw();
        }
    }

    [[nodiscard]] bool hyperlink_modifier_pressed() const noexcept
    {
        switch (window_config_.hyperlink)
        {
        case hyperlink_modifier::none:
            return true;
        case hyperlink_modifier::control:
            return (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        case hyperlink_modifier::alt:
            return (::GetKeyState(VK_MENU) & 0x8000) != 0;
        case hyperlink_modifier::shift:
            return (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        }
        return false;
    }

    [[nodiscard]] bool open_hyperlink(HWND hwnd, LPARAM lparam)
    {
        std::unique_lock<win32::srwlock> lock{console_mutex_};
        const auto [x, y] = view_cell_at(hwnd, lparam);
        const auto link = console_.cell_at_virtual(y, x).hyperlink();
        if (link == screen::no_cell_hyperlink)
        {
            return false;
        }

        const auto &target = console_.buffer().hyperlinks().at(link);
        if (target.uri.empty())
        {
            return false;
        }

        std::wstring uri;
        uri.reserve(target.uri.size());
        for (const auto character : target.uri)
        {
            uri.push_back(static_cast<wchar_t>(character));
        }
        (void)::ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }

    void auto_scroll_selection(HWND hwnd)
    {
        if (!selection_dragging_)
        {
            ::KillTimer(hwnd, selection_scroll_timer);
            return;
        }

        RECT client{};
        if (::GetClientRect(hwnd, &client) == FALSE)
        {
            return;
        }

        const auto row_height = static_cast<double>(console_.cell_height());
        const auto edge_rows = static_cast<double>(std::max<std::size_t>(1, window_config_.selection_auto_scroll_edge_rows));
        const auto edge = static_cast<LONG>(std::ceil(row_height * edge_rows));
        const bool scroll_up = selection_drag_point_.y < edge;
        const bool scroll_down = selection_drag_point_.y >= client.bottom - edge;
        if (!scroll_up && !scroll_down)
        {
            return;
        }

        console_.scroll_by_pixels(scroll_up ? row_height : -row_height);

        const auto x = std::clamp<LONG>(selection_drag_point_.x, 0, std::max<LONG>(0, client.right - 1));
        const auto y = scroll_up ? 0 : std::max<LONG>(0, client.bottom - 1);
        const auto cell = view_cell_at(hwnd, MAKELPARAM(x, y));
        console_.set_selection(selection_anchor_x_, selection_anchor_y_,
                               console_.snap_selection_column(cell.first, cell.second), cell.second);
        notify_uia_selection();
        worker_.request_redraw();
    }

    [[nodiscard]] bool mouse_tracking() const noexcept
    {
        return console_.mouse_reporting();
    }

    void push_mouse(LPARAM lparam, std::uint32_t button_state, std::uint32_t event_flags)
    {
        const auto [x, y] = cell_at(window_, lparam);
        if (console_.virtual_terminal_input() && console_.mouse_sgr_encoding())
        {
            auto button = 3;
            if ((event_flags & MOUSE_WHEELED) != 0)
            {
                const auto delta = static_cast<short>(HIWORD(button_state));
                button = delta > 0 ? 64 : 65;
            }
            else if ((event_flags & MOUSE_MOVED) != 0)
            {
                button = (button_state & FROM_LEFT_1ST_BUTTON_PRESSED) != 0
                             ? 0
                             : (button_state & FROM_LEFT_2ND_BUTTON_PRESSED) != 0
                                   ? 2
                                   : (button_state & FROM_LEFT_3RD_BUTTON_PRESSED) != 0 ? 1 : 3;
                button += 32;
            }
            else if ((button_state & FROM_LEFT_1ST_BUTTON_PRESSED) != 0)
            {
                button = 0;
            }
            else if ((button_state & FROM_LEFT_2ND_BUTTON_PRESSED) != 0)
            {
                button = 2;
            }
            else if ((button_state & FROM_LEFT_3RD_BUTTON_PRESSED) != 0)
            {
                button = 1;
            }

            const auto modifiers = control_state();
            if ((modifiers & SHIFT_PRESSED) != 0)
            {
                button += 4;
            }
            if ((modifiers & LEFT_ALT_PRESSED) != 0 || (modifiers & RIGHT_ALT_PRESSED) != 0)
            {
                button += 8;
            }
            if ((modifiers & LEFT_CTRL_PRESSED) != 0 || (modifiers & RIGHT_CTRL_PRESSED) != 0)
            {
                button += 16;
            }

            consoleinput::input_event text_event{};
            text_event.type = consoleinput::input_event_type::text;
            const bool release = button_state == 0 && (event_flags & MOUSE_MOVED) == 0 &&
                                 (event_flags & MOUSE_WHEELED) == 0;
            LOG2("push sgr mouse button=%d x=%zu y=%zu release=%d", button, x, y, release ? 1 : 0);
            vtserializer::append_mouse_sgr(text_event.text, button, static_cast<int>(x), static_cast<int>(y),
                                           release);
            console_.push_input(std::move(text_event));
            worker_.request_redraw();
            return;
        }

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
        if (!clipboard_write_allowed(clipboard_config_))
        {
            return;
        }
        auto &text = copy_buffer_;
        console_.selected_text_into(text);
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
        if (!clipboard_read_allowed(clipboard_config_))
        {
            return;
        }
        console_.clear_selection();
        if (::OpenClipboard(hwnd) == FALSE)
        {
            return;
        }
        if (auto *data = ::GetClipboardData(CF_UNICODETEXT); data != nullptr)
        {
            if (auto *text = static_cast<const char16_t *>(::GlobalLock(data)); text != nullptr)
            {
                std::size_t text_length = 0;
                while (text[text_length] != 0)
                {
                    ++text_length;
                }
                const auto default_limit = console_.buffer().width() * console_.buffer().height() * sizeof(char16_t);
                const auto limit = clipboard_config_.paste_max_size == 0 ? default_limit
                                                                          : clipboard_config_.paste_max_size;
                if (text_length * sizeof(char16_t) > limit)
                {
                    LOG2("paste rejected length=%zu limit=%zu", text_length, limit);
                    ::GlobalUnlock(data);
                    ::CloseClipboard();
                    return;
                }
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
                LOG2("paste clipboard pushed");
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

        if (message == WM_NCCREATE)
        {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }

        // WM_NCCREATE installs the instance pointer. All other messages can
        // return early while the window is still being created or already torn
        // down, instead of wrapping every case in an extra null check.
        if (self == nullptr)
        {
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }

        switch (message)
        {
        case WM_SYSCOMMAND:
            if (!self->window_behavior_config_.allow_move_resize &&
                ((wparam & 0xFFF0) == SC_SIZE || (wparam & 0xFFF0) == SC_MOVE))
            {
                return 0;
            }
            break;
        case WM_GETMINMAXINFO:
        {
            const auto dpi = std::max<UINT>(96, ::GetDpiForWindow(hwnd));
            const auto [min_width, min_height] = initial_client_size(
                dpi, self->font_config_, self->window_config_.min_columns, self->window_config_.min_rows);
            RECT minimum{0, 0, min_width, min_height};
            ::AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP, dpi);
            reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize =
                POINT{minimum.right - minimum.left, minimum.bottom - minimum.top};
            return 0;
        }
        case WM_SIZE:
        case WM_DISPLAYCHANGE:
            self->worker_.request_redraw();
            return 0;
        case WM_DPICHANGED:
        {
            const auto *suggested = reinterpret_cast<const RECT *>(lparam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                           suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            self->worker_.request_redraw();
            return 0;
        }
        case WM_IME_REQUEST:
            return self->handle_ime_request(wparam, lparam);
        case WM_SETFOCUS:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            self->console_.set_focused(true);
            lock.unlock();
            self->tsf_.focus();
            self->worker_.request_redraw();
            return 0;
        }
        case WM_KILLFOCUS:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            self->console_.set_focused(false);
            lock.unlock();
            self->tsf_.unfocus();
            self->worker_.request_redraw();
            return 0;
        }
        case WM_APP + 0x102:
        {
            LOG2("bell flash");
            FLASHWINFO flash{sizeof(FLASHWINFO), hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 1, 0};
            (void)::FlashWindowEx(&flash);
            return 0;
        }
        case WM_APP + 0x101:
            if (self->window_config_.uia_text_changed_events && self->uia_events_)
            {
                (void)self->uia_events_->NotifyTextChanged();
            }
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == UiaRootObjectId)
            {
                auto *provider = self->ensure_uia_provider();
                if (provider != nullptr)
                {
                    return ::UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
                }
            }
            break;
        case WM_TIMER:
            if (wparam == selection_scroll_timer)
            {
                self->auto_scroll_selection(hwnd);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
        {
            const auto delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (delta == 0)
            {
                return 0;
            }

            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ::ScreenToClient(hwnd, &point);
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            const auto geometry = self->scrollbar_geometry(hwnd);
            const auto over_scrollbar =
                scrollbar::hit_test(geometry, static_cast<float>(point.x), static_cast<float>(point.y)) !=
                scrollbar::hit_target::none;
            if (!over_scrollbar && (::GetKeyState(VK_SHIFT) & 0x8000) == 0 &&
                self->console_.alternate_active() && self->console_.vt_mouse_tracking())
            {
                const auto packed = static_cast<LPARAM>((point.y << 16) | (point.x & 0xFFFF));
                self->push_mouse(packed, static_cast<std::uint32_t>(static_cast<std::uint16_t>(delta)) << 16,
                                 MOUSE_WHEELED);
                return 0;
            }

            self->console_.scroll_by(static_cast<double>(delta) *
                                     static_cast<double>(self->window_config_.mouse_wheel_lines) /
                                     static_cast<double>(WHEEL_DELTA));
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            self->update_scrollbar_hover(hwnd, lparam);
            self->update_hyperlink_hover(hwnd, lparam);
            if (self->scrollbar_dragging_)
            {
                self->handle_scrollbar_mouse_move(hwnd, lparam);
                return 0;
            }

            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            if (self->selection_dragging_)
            {
                self->update_selection(hwnd, lparam);
                return 0;
            }
            if (self->console_.vt_mouse_tracking())
            {
                self->push_mouse(lparam, (::GetKeyState(VK_LBUTTON) & 0x8000) != 0
                                           ? FROM_LEFT_1ST_BUTTON_PRESSED
                                           : 0,
                                 MOUSE_MOVED);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
        {
            self->tracking_mouse_leave_ = false;
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            self->console_.set_scrollbar_hovered(false);
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            LOG2("lbutton reporting=%d vt=%d sgr=%d mode=%08x", self->console_.mouse_reporting() ? 1 : 0,
                 self->console_.vt_mouse_tracking() ? 1 : 0, self->console_.mouse_sgr_encoding() ? 1 : 0,
                 self->console_.state().input_mode);
            self->worker_.request_atlas_upload();
            const auto x = static_cast<float>(GET_X_LPARAM(lparam));
            const auto y = static_cast<float>(GET_Y_LPARAM(lparam));
            bool over_scrollbar = false;
            {
                std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                const auto geometry = self->scrollbar_geometry(hwnd);
                over_scrollbar = scrollbar::hit_test(geometry, x, y) != scrollbar::hit_target::none;
            }
            if (over_scrollbar)
            {
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    self->console_.set_scrollbar_hovered(true);
                }
                self->handle_scrollbar_mouse_down(hwnd, lparam);
                return 0;
            }

            if (self->hyperlink_modifier_pressed() && self->open_hyperlink(hwnd, lparam))
            {
                return 0;
            }

            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            if ((::GetKeyState(VK_SHIFT) & 0x8000) == 0 && self->console_.vt_mouse_tracking())
            {
                self->push_mouse(lparam, FROM_LEFT_1ST_BUTTON_PRESSED, 0);
                return 0;
            }
            self->begin_selection(hwnd, lparam);
            return 0;
        }
        case WM_RBUTTONDOWN:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            if (self->console_.vt_mouse_tracking() && (::GetKeyState(VK_SHIFT) & 0x8000) == 0)
            {
                self->push_mouse(lparam, RIGHTMOST_BUTTON_PRESSED, 0);
            }
            return 0;
        }
        case WM_RBUTTONUP:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            LOG2("rbuttonup selection=%d vt=%d", self->console_.has_selection() ? 1 : 0,
                 self->console_.vt_mouse_tracking() ? 1 : 0);
            if (self->console_.vt_mouse_tracking() && (::GetKeyState(VK_SHIFT) & 0x8000) == 0)
            {
                self->push_mouse(lparam, 0, 0);
                return 0;
            }
            if (self->window_config_.right_click == right_click_action::copy)
            {
                if (self->console_.has_selection())
                {
                    self->copy_selection(hwnd);
                    self->console_.clear_selection();
                    self->worker_.request_redraw();
                }
                return 0;
            }
            if (self->window_config_.right_click == right_click_action::paste)
            {
                self->paste_clipboard(hwnd);
                return 0;
            }
            if (self->console_.has_selection())
            {
                self->copy_selection(hwnd);
                self->console_.clear_selection();
                self->worker_.request_redraw();
                return 0;
            }
            self->paste_clipboard(hwnd);
            return 0;
        }
        case WM_MBUTTONDOWN:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            if (self->console_.vt_mouse_tracking())
            {
                self->push_mouse(lparam, FROM_LEFT_2ND_BUTTON_PRESSED, 0);
            }
            return 0;
        }
        case WM_MBUTTONUP:
        {
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            if (self->console_.vt_mouse_tracking())
            {
                self->push_mouse(lparam, 0, 0);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        {
            if (self->selection_dragging_)
            {
                self->selection_dragging_ = false;
                ::KillTimer(hwnd, selection_scroll_timer);
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
            if (self->console_.vt_mouse_tracking())
            {
                self->push_mouse(lparam, 0, 0);
            }
            return 0;
        }
        case WM_KEYDOWN:
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
            if (!handled)
            {
                return ::DefWindowProcW(hwnd, message, wparam, lparam);
            }

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
        case WM_KEYUP:
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
            return 0;
        }
        // Chromium's TSF path still treats WM_CHAR/WM_SYSCHAR as text input
        // when the TIP does not consume the key, for example an inactive TIP
        // or an English keyboard profile. Keep both paths identical here.
        case WM_CHAR:
        case WM_SYSCHAR:
        {
            const auto character = static_cast<char16_t>(wparam);
            if ((character == 0x03 || character == 0x16 || character == 0x13) &&
                (::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                (::GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                return 0;
            }
            if (character < 0x20 && character != u'\r' && character != u'\n' && character != u'\b' &&
                character != u'\t' && character != 0x1B && !(character >= 1 && character <= 26))
            {
                return 0;
            }

            auto event = consoleinput::input_event{
                .type = consoleinput::input_event_type::key,
                .key = character_key(character),
            };
            std::unique_lock<win32::srwlock> lock{self->console_mutex_};
            self->console_.clear_selection();
            self->console_.snap_to_bottom_if_unlocked();
            self->console_.push_input(std::move(event));
            return 0;
        }
        case WM_CLOSE:
            self->worker_.stop();
            ::DestroyWindow(hwnd);
            return 0;
        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            self->worker_.stop();
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }
    bool window_ready_{};
    // UI-thread scratch buffers. Keeping capacity across IME reconversion,
    // copy, and client-exit title updates avoids a fresh allocation per
    // message.
    std::u16string ime_text_buffer_;
    std::u16string copy_buffer_;
    std::wstring window_title_buffer_;
    win32::wcstring_view title_;
    console &console_;
    win32::srwlock &console_mutex_;
    const clipboard_config &clipboard_config_;
    const background_config &background_config_;
    const color_config &color_config_;
    const bell_config &bell_config_;
    const font_config &font_config_;
    const window_behavior_config &window_behavior_config_;
    const title_config &title_config_;
    winrt::com_ptr<IRawElementProviderSimple> uia_provider_;
    winrt::com_ptr<IUiaEvents> uia_events_;
    win32::handle_view client_process_;
    const console_config &console_config_;
    const window_config &window_config_;
    win32::handle_view damage_event_;
    render_worker worker_;
    HWND window_;
    tsf_host tsf_;
    bool client_exited_{};
    bool scrollbar_dragging_{};
    bool selection_dragging_{};
    POINT selection_drag_point_{};
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
