// ── conpty/pseudo_window.hpp ─────────────────────────────
// GetConsoleWindow 的不可见伪窗口。
//
// 需求与取舍见 doc/design/0002.md：
// - 类名沿用 PseudoConsoleWindow，样式 WS_POPUP + WS_EX_TOOLWINDOW |
//   WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP；
// - 不是 HWND_MESSAGE（依赖方会 EnumWindows/FindWindow），也不是 child
//   window（坐标语义会变）；
// - 独立线程跑 GetMessage 循环，没有消息时不唤醒；
// - 窗口线程不直接访问控制台状态：可见性/关闭只置位事件，由主线程处理；
// - 标题由主线程在构造时传入，作为 CreateWindowEx 的初始窗口文本。
#pragma once
#include <windows.h>
#include <algorithm>

#include <cstring>
#include <string>
#include <string_view>
#include <cstdlib>
#include <cassert>
#include "char_convert.hpp"
#include "utility/log.hpp"
#include <utility>
#include "win32/event.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

namespace corehost::conpty
{

class pseudo_window
{
  public:
    pseudo_window(win32::handle_view server, std::u32string_view title, COORD cells)
        : _server{server}, _cells{cells}, _ready{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
          _close_requested{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
          _show_event{win32::create_tag, /*manual_reset=*/false, /*initial=*/false},
          _hide_event{win32::create_tag, /*manual_reset=*/false, /*initial=*/false}
    {
        convert_u32_to_wstr(title, _initial_title);
    }

    pseudo_window(const pseudo_window &) = delete;
    pseudo_window &operator=(const pseudo_window &) = delete;
    ~pseudo_window()
    {
        // start() 可能因创建线程失败而抛出；此时没有可 join 的线程。
        if (_thread.valid())
            stop_and_join();
    }

    // 启动窗口线程并等待窗口创建完成。创建失败是致命错误：GetConsoleWindow
    // 的契约无法满足。
    void start()
    {
        assert(!_thread.valid());
        _thread = win32::basic_thread{&pseudo_window::thread_entry, this, {.name = L"corehost-window"}};
        _ready.wait();
    }

    // 通过窗口消息请求退出；窗口过程 PostQuitMessage 后 join 无限等待。
    void stop_and_join()
    {
        assert(_thread.valid());
        if (_hwnd != nullptr && ::IsWindow(_hwnd) != FALSE)
        {
            if (::PostMessageW(_hwnd, wm_stop_thread, 0, 0) == FALSE)
                win32::throw_last_error();
        }

        _thread.join();
    }

    [[nodiscard]] HWND hwnd() const noexcept
    {
        return _hwnd;
    }

    [[nodiscard]] win32::handle_view close_requested() const noexcept
    {
        return _close_requested.view();
    }

    [[nodiscard]] win32::handle_view show_event() const noexcept
    {
        return _show_event.view();
    }

    [[nodiscard]] win32::handle_view hide_event() const noexcept
    {
        return _hide_event.view();
    }

    // WT 发起的可见性/尺寸变化必须由窗口线程执行；窗口过程不获取 message_router 锁。
    void set_visibility(bool visible)
    {
        if (_hwnd == nullptr || ::IsWindow(_hwnd) == FALSE)
            return;
        ::SendMessageW(_hwnd, wm_set_visibility, visible ? TRUE : FALSE, 0);
    }

    void set_size(COORD cells)
    {
        if (_hwnd == nullptr || ::IsWindow(_hwnd) == FALSE)
            return;
        ::SendMessageW(_hwnd, wm_set_size, 0, static_cast<LPARAM>(MAKELPARAM(cells.X, cells.Y)));
    }

    // GWLP_HWNDPARENT 是 owner 的唯一存储；不在这里缓存。
    void set_owner(HWND owner)
    {
        if (_hwnd == nullptr || ::IsWindow(_hwnd) == FALSE)
            return;
        ::SetWindowLongPtrW(_hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    }

  private:
    static DWORD WINAPI thread_entry(LPVOID self)
    {
        static_cast<pseudo_window *>(self)->run();
        return 0;
    }

    void run()
    {
        LOG("pseudo_window: thread enter");

        // 类级样式：不可见、无重定向表面，不抢焦点。
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &pseudo_window::window_proc;
        wc.hInstance = nullptr;
        wc.lpszClassName = class_name;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;

        // 一个进程只创建一个伪窗口；类注册失败是致命错误。
        if (::RegisterClassExW(&wc) == 0)
            win32::throw_last_error();

        // WS_POPUP 是硬需求：否则 GetAncestor(GetConsoleWindow(), GA_ROOTOWNER)
        // 会返回伪窗口自己，而不是宿主终端窗口。
        const int width = _cells.X > 0 ? static_cast<int>(_cells.X) * cell_width : 640;
        const int height = _cells.Y > 0 ? static_cast<int>(_cells.Y) * cell_height : 400;
        const HWND created = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                                               class_name, _initial_title.c_str(), WS_POPUP, 0, 0, width, height,
                                               nullptr, nullptr, nullptr, this);
        if (created == nullptr)
            win32::throw_last_error();

        _hwnd = created;
        _ready.set();
        LOG("pseudo_window: created hwnd=%p", created);

        MSG message{};
        for (;;)
        {
            const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
            if (result == 0)
                break; // WM_QUIT
            if (result == -1)
                win32::throw_last_error();

            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }

        LOG("pseudo_window: thread exit");
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
            return ::DefWindowProcW(hwnd, message, wparam, lparam);

        switch (message)
        {
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED)
                self->post_visibility(false);
            else if (wparam == SIZE_RESTORED)
                self->post_visibility(true);
            return 0;
        case WM_SHOWWINDOW:
            if (lparam == 0)
                self->post_visibility(wparam != 0);
            return 0;
        case WM_CLOSE:
            LOG("pseudo_window: WM_CLOSE requested");
            self->_close_requested.set();
            self->cancel_server_io();
            return 0;
        case wm_stop_thread:
            ::PostQuitMessage(0);
            return 0;
        case wm_set_visibility:
            self->_suppress_visibility = true;
            ::ShowWindow(hwnd, wparam != 0 ? SW_SHOWNOACTIVATE : SW_MINIMIZE);
            self->_suppress_visibility = false;
            return 0;
        case wm_set_size: {
            const COORD cells{static_cast<SHORT>(LOWORD(lparam)), static_cast<SHORT>(HIWORD(lparam))};
            const int width = cells.X > 0 ? static_cast<int>(cells.X) * cell_width : 640;
            const int height = cells.Y > 0 ? static_cast<int>(cells.Y) * cell_height : 400;
            self->_suppress_visibility = true;
            ::SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            self->_suppress_visibility = false;
            return 0;
        }
        case WM_DESTROY:
            LOG("pseudo_window: WM_DESTROY");
            self->_close_requested.set();
            self->cancel_server_io();
            ::PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE: {
            if (LOWORD(wparam) == WA_INACTIVE)
                return 0;
            const auto owner = reinterpret_cast<HWND>(::GetWindowLongPtrW(hwnd, GWLP_HWNDPARENT));
            if (owner != nullptr && ::IsWindow(owner) != FALSE)
                ::SetFocus(owner);
            return 0;
        }
        case WM_GETOBJECT:
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    // 窗口线程只置位事件；可见性由主线程消费并写 VT 输出。
    void post_visibility(bool visible) noexcept
    {
        if (_suppress_visibility)
            return;
        if (visible)
        {
            _hide_event.reset();
            _show_event.set();
        }
        else
        {
            _show_event.reset();
            _hide_event.set();
        }
        cancel_server_io();
    }

    static constexpr UINT wm_stop_thread = WM_APP + 1;
    static constexpr UINT wm_set_visibility = WM_APP + 2;
    static constexpr UINT wm_set_size = WM_APP + 3;
    static constexpr int cell_width = 8;
    static constexpr int cell_height = 16;

    // 打断主线程阻塞中的 READ_IO，让主循环处理可见性/关闭事件。
    void cancel_server_io() const noexcept
    {
        if (!_server.valid())
            return;
        if (::CancelIoEx(_server.get(), nullptr) != FALSE)
            return;
        const auto err = win32::get_last_error();
        if (err == win32::error::not_found)
            return;
        LOG("pseudo_window: CancelIoEx failed err=%u", static_cast<unsigned>(err));
        std::abort();
    }
    static constexpr const wchar_t *class_name = L"PseudoConsoleWindow";

    win32::handle_view _server;
    COORD _cells{};
    bool _suppress_visibility = false; // 仅窗口线程访问
    win32::event _ready;
    win32::event _close_requested;
    win32::event _show_event;
    win32::event _hide_event;
    win32::basic_thread _thread;
    HWND _hwnd = nullptr;

    // 构造时写入，窗口线程创建窗口时读取。
    std::wstring _initial_title;
};

} // namespace corehost::conpty