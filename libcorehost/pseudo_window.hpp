// ── conpty/pseudo_window.hpp ─────────────────────────────
// GetConsoleWindow 的不可见伪窗口。
//
// 需求与取舍见 doc/design/0002.md：
// - 类名沿用 PseudoConsoleWindow，样式 WS_POPUP + WS_EX_TOOLWINDOW |
//   WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP；
// - 不是 HWND_MESSAGE（依赖方会 EnumWindows/FindWindow），也不是 child
//   window（坐标语义会变）；
// - 独立线程跑 GetMessage 循环，没有消息时不唤醒；
// - 窗口线程不直接读写控制台状态：关闭请求只置位事件，由主线程执行。
//
// 每个进程一个控制台会话，因此实例是进程级单例；GetConsoleWindow 的 API
// handler 直接取 instance().hwnd()。
#pragma once
#include <windows.h>
#include <atomic>
#include <utility>
#include "utility/log.hpp"
#include "win32/event.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/thread.hpp"

namespace corehost::conpty
{

class pseudo_window
{
  public:
    static pseudo_window &instance()
    {
        static pseudo_window window;
        return window;
    }

    pseudo_window(const pseudo_window &) = delete;
    pseudo_window &operator=(const pseudo_window &) = delete;
    ~pseudo_window()
    {
        stop_and_join();
    }

    // 启动窗口线程并等待窗口创建完成。创建失败是致命错误：GetConsoleWindow
    // 的契约无法满足。
    void start()
    {
        if (_thread.valid())
            return;

        _thread = win32::basic_thread{&pseudo_window::thread_entry, this, {.name = L"corehost-window"}};
        _ready.wait();
    }

    // 通知窗口线程退出并无限等待。PostThreadMessage(WM_QUIT) 让 GetMessage
    // 返回 0，线程退出；join 不设超时。
    void stop_and_join()
    {
        if (!_thread.valid())
            return;

        if (::PostThreadMessageW(_thread.id(), WM_QUIT, 0, 0) == FALSE)
            win32::throw_last_error();

        _thread.join();
        _hwnd = nullptr;
    }

    [[nodiscard]] HWND hwnd() const noexcept
    {
        return _hwnd.load(std::memory_order_acquire);
    }

    // 窗口收到 WM_CLOSE 时置位；主线程用它触发与终端关闭一致的退出路径。
    [[nodiscard]] win32::handle_view close_requested() const noexcept
    {
        return _close_requested.view();
    }

  private:
    pseudo_window() : _ready{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
                      _close_requested{win32::create_tag, /*manual_reset=*/true, /*initial=*/false}
    {
    }

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

        if (::RegisterClassExW(&wc) == 0)
        {
            const auto err = win32::get_last_error();
            if (err != win32::error::class_already_exists)
                win32::throw_last_error(err);
        }

        // WS_POPUP 是硬需求：否则 GetAncestor(GetConsoleWindow(), GA_ROOTOWNER)
        // 会返回伪窗口自己，而不是宿主终端窗口。
        const HWND created = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                                               class_name, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr,
                                               this);
        if (created == nullptr)
            win32::throw_last_error();

        _hwnd.store(created, std::memory_order_release);
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
        case WM_CLOSE:
            // 不在这里销毁窗口：关闭语义由会话主线程统一执行。
            LOG("pseudo_window: WM_CLOSE requested");
            self->_close_requested.set();
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE:
            // 不可见窗口不能被激活；让系统继续默认处理。
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_GETOBJECT:
            // 不提供 UIA provider；返回 0 表示没有可访问对象。
            return 0;
        default:
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    static constexpr const wchar_t *class_name = L"PseudoConsoleWindow";

    win32::event _ready;
    win32::event _close_requested;
    win32::basic_thread _thread;
    std::atomic<HWND> _hwnd{nullptr};
};

} // namespace corehost::conpty
