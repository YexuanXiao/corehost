#include "window_host.hpp"

#include "win32/error.hpp"

#include <utility>

namespace corehost::window
{
namespace
{
constexpr const wchar_t *class_name = L"CoreHostWindow";
} // namespace

window_host::window_host(std::wstring title) : title_{std::move(title)}
{
    if (title_.empty())
    {
        title_ = L"CoreHost";
    }
}

window_host::~window_host() = default;

LRESULT CALLBACK window_host::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *self = reinterpret_cast<window_host *>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_NCCREATE:
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
        return ::DefWindowProcW(hwnd, message, wparam, lparam);
    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;
    case WM_SIZE:
        if (self != nullptr && self->renderer_)
        {
            self->renderer_->resize(LOWORD(lparam), HIWORD(lparam));
        }
        return 0;
    case WM_PAINT:
        if (self != nullptr && self->renderer_)
        {
            self->renderer_->render();
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

int window_host::run()
{
    const auto instance = ::GetModuleHandleW(nullptr);

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

    const HWND window = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, class_name, title_.c_str(), WS_OVERLAPPEDWINDOW,
                                          CW_USEDEFAULT, CW_USEDEFAULT, 800, 500, nullptr, nullptr, instance, this);
    if (window == nullptr)
    {
        win32::throw_last_error();
    }

    renderer_ = std::make_unique<d3d12ui::d3d12_window_renderer>(window);
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

} // namespace corehost::window
