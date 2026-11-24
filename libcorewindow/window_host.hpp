#pragma once

#include "d3d12_window_renderer.hpp"
#include "console.hpp"
#include "win32/srwlock.hpp"
#include "cell_atlas.hpp"
#include "text_engine.hpp"

#include "win32/error.hpp"
#include "win32/string.hpp"

#include <Windows.h>

#include <array>
#include <cassert>
#include <mutex>
#include <vector>

namespace corehost::window
{

// Header-only top-level Win32 host for window mode. The D3D12 renderer is a
// direct stack member; there is only one implementation and no heap owner.
class window_host final
{
  public:
    window_host(win32::wcstring_view title, console &console, win32::srwlock &console_mutex)
        : title_{title}, console_{console}, console_mutex_{console_mutex}
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

        renderer_.initialize(window);
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

        renderer_.reset();
        return static_cast<int>(message.wParam);
    }

  private:
    static constexpr std::size_t columns = 120;
    static constexpr std::size_t rows = 30;

    void apply_screen_updates()
    {
        for (const auto &row : updates_)
        {
            if (row.y >= rows)
            {
                continue;
            }

            auto x = row.begin;
            for (const auto &cell : row.cells)
            {
                if (x >= columns)
                {
                    break;
                }
                const auto index = row.y * columns + x;
                cells_[index] = row.text.substr(cell.text_offset, cell.text_length);
                cell_state_[index] = cell;
                ++x;
            }
        }
    }

    void build_quads()
    {
        quads_.clear();
        const auto metrics = text_.metrics();
        const auto cell_width = metrics.advance;
        const auto cell_height = metrics.ascent + metrics.descent + metrics.line_gap;
        const auto atlas_scale = 1.0f / static_cast<float>(atlas_.size());

        for (std::size_t y = 0; y < rows; ++y)
        {
            for (std::size_t x = 0; x < columns; ++x)
            {
                const auto index = y * columns + x;
                if (screen::cell_width_ops::is_tail(cell_state_[index].width))
                {
                    continue;
                }
                const auto &text = cells_[index];
                const auto *region = atlas_.find_or_add(text);
                if (region == nullptr)
                {
                    continue;
                }

                const auto left = static_cast<float>(x) * cell_width + static_cast<float>(region->left);
                const auto top = static_cast<float>(y) * cell_height + metrics.ascent + static_cast<float>(region->top);
                quads_.push_back(d3d12ui::text_quad{
                    .x = left,
                    .y = top,
                    .width = static_cast<float>(region->width),
                    .height = static_cast<float>(region->height),
                    .u0 = static_cast<float>(region->x) * atlas_scale,
                    .v0 = static_cast<float>(region->y) * atlas_scale,
                    .u1 = static_cast<float>(region->x + region->width) * atlas_scale,
                    .v1 = static_cast<float>(region->y + region->height) * atlas_scale,
                });
            }
        }

        if (atlas_.dirty())
        {
            renderer_.set_text_texture(atlas_.pixels(), atlas_.size(), atlas_.size());
            atlas_.clear_dirty();
        }
        renderer_.set_text_quads(quads_);
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
            ::DestroyWindow(hwnd);
            return 0;
        case WM_SIZE:
            if (self != nullptr)
            {
                self->renderer_.resize(LOWORD(lparam), HIWORD(lparam));
            }
            return 0;
        case WM_PAINT:
            if (self != nullptr)
            {
                self->updates_.clear();
                {
                    std::unique_lock<win32::srwlock> lock{self->console_mutex_};
                    (void)self->console_.copy_updates(self->updates_);
                }
                self->apply_screen_updates();
                self->build_quads();
                self->renderer_.render();
            }
            ::ValidateRect(hwnd, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (self != nullptr)
            {
                self->renderer_.reset();
            }
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    win32::wcstring_view title_;
    d3d12ui::d3d12_window_renderer renderer_;
    console &console_;
    win32::srwlock &console_mutex_;
    std::vector<screen_row_update> updates_;
    uitext::text_engine text_{fallback_families(), 16.0f};
    uitext::cell_atlas atlas_{text_};
    std::vector<std::u16string> cells_ = std::vector<std::u16string>(120u * 30u);
    std::vector<screen_cell_update> cell_state_ = std::vector<screen_cell_update>(120u * 30u);
    std::vector<d3d12ui::text_quad> quads_;

    static std::array<uitext::font_fallback_entry, 3> fallback_families()
    {
        return {
            uitext::font_fallback_entry{L"Cascadia Code"},
            uitext::font_fallback_entry{L"Cascadia Mono"},
            uitext::font_fallback_entry{L"Consolas"},
        };
    }
};

} // namespace corehost::window
