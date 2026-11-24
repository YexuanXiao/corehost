#pragma once

#include "d3d12_window_renderer.hpp"
#include "screen_updates.hpp"
#include "text_engine.hpp"

#include "win32/error.hpp"
#include "win32/string.hpp"

#include <Windows.h>

#include <array>
#include <cassert>
#include <vector>

namespace corehost::window
{

// Header-only top-level Win32 host for window mode. The D3D12 renderer is a
// direct stack member; there is only one implementation and no heap owner.
class window_host final
{
  public:
    window_host(win32::wcstring_view title, screen_update_source source) : title_{title}, source_{source}
    {
        assert(!title_.empty());
        assert(source_.take != nullptr);
        assert(source_.attach != nullptr);
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
                              CW_USEDEFAULT, 800, 500, nullptr, nullptr, instance, this);
        if (window == nullptr)
        {
            win32::throw_last_error();
        }

        source_.attach(source_.context, window);
        renderer_.initialize(window);
        ::ShowWindow(window, SW_SHOWDEFAULT);
        ::UpdateWindow(window);

        std::vector<uitext::glyph_bitmap> glyphs;
        text_.rasterize(u"CoreHost", glyphs);
        if (!glyphs.empty() && !glyphs.front().alpha.empty())
        {
            renderer_.set_text_texture(glyphs.front().alpha, static_cast<UINT>(glyphs.front().width),
                                       static_cast<UINT>(glyphs.front().height));
            ::InvalidateRect(window, nullptr, FALSE);
            renderer_.render();
        }

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
        case screen_update_message:
            ::InvalidateRect(hwnd, nullptr, FALSE);
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
                (void)self->source_.take(self->source_.context, self->updates_);
                self->renderer_.render();
            }
            ::ValidateRect(hwnd, nullptr);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (self != nullptr)
            {
                self->source_.attach(self->source_.context, nullptr);
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
    screen_update_source source_;
    std::vector<screen_row_update> updates_;
    uitext::text_engine text_{fallback_families(), 32.0f};

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
