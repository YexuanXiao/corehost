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
// - 标题在 start 前由主线程初始化；start 后经 WM_SETTEXT 交给窗口线程。
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
    explicit pseudo_window(win32::handle_view server)
        : _server{server}, _ready{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
          _close_requested{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
          _show_event{win32::create_tag, /*manual_reset=*/false, /*initial=*/false},
          _hide_event{win32::create_tag, /*manual_reset=*/false, /*initial=*/false}
    {
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
        if (::PostMessageW(_hwnd, wm_stop_thread, 0, 0) == FALSE)
            win32::throw_last_error();

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

    // start() 前直接初始化窗口线程的标题；start() 后通过 WM_SETTEXT 交给窗口线程。
    void set_title(std::u32string_view title)
    {
        std::wstring converted;
        convert_u32_to_wstr(title, converted);
        if (_hwnd == nullptr)
        {
            // start() 前窗口线程尚未运行，直接初始化窗口线程自己的标题。
            _title = std::move(converted);
            return;
        }
        if (::SendMessageW(_hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(converted.c_str())) == FALSE)
            win32::throw_last_error();
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
        const HWND created = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                                               class_name, L"", WS_POPUP, 0, 0, 640, 400, nullptr, nullptr, nullptr,
                                               this);
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
        case WM_DESTROY:
            return 0;
        case WM_ACTIVATE:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_GETOBJECT:
            return 0;
        case WM_SETTEXT:
            if (lparam != 0)
                self->_title = reinterpret_cast<const wchar_t *>(lparam);
            else
                self->_title.clear();
            return TRUE;
        case WM_GETTEXTLENGTH:
            return static_cast<LRESULT>(self->_title.size());
        case WM_GETTEXT: {
            const auto capacity = static_cast<size_t>(wparam);
            if (capacity != 0)
            {
                const auto count = capacity > self->_title.size() ? self->_title.size() : capacity - 1;
                std::memcpy(reinterpret_cast<wchar_t *>(lparam), self->_title.data(), count * sizeof(wchar_t));
                reinterpret_cast<wchar_t *>(lparam)[count] = L'\0';
            }
            return static_cast<LRESULT>(self->_title.size());
        }
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    // 窗口线程只置位事件；可见性由主线程消费并写 VT 输出。
    void post_visibility(bool visible) noexcept
    {
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
    win32::event _ready;
    win32::event _close_requested;
    win32::event _show_event;
    win32::event _hide_event;
    win32::basic_thread _thread;
    HWND _hwnd = nullptr;


    // start 前由主线程初始化；start 后只由窗口线程读写。
    std::wstring _title;
};

} // namespace corehost::conpty