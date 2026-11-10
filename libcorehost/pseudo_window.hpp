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
// 实例由会话装配处持有，hwnd 通过 console_state 传给 GetConsoleWindow 的
// API handler；不使用单例、不使用全局变量。
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdlib>
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
    // 会话装配处构造一个实例，按引用/指针传给需要它的组件；不使用单例。
    pseudo_window() : _ready{win32::create_tag, /*manual_reset=*/true, /*initial=*/false},
                      _close_requested{win32::create_tag, /*manual_reset=*/true, /*initial=*/false}
    {
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

    // ConDrv server 句柄：WM_CLOSE 时用 CancelIoEx 打断主线程阻塞中的
    // READ_IO。必须在 start() 前设置；句柄所有权仍归会话。
    void set_server(win32::handle_view server) noexcept
    {
        _server = server;
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
            // 不在这里销毁窗口：关闭语义由会话主线程统一执行。主线程此时通常
            // 阻塞在 READ_IO 内部，必须先打断它才能让主循环看到关闭请求。
            LOG("pseudo_window: WM_CLOSE requested");
            self->_close_requested.set();
            self->cancel_server_io();
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

    // 打断主线程阻塞中的 READ_IO。线程不在同步 I/O 中时 CancelSynchronousIo
    // 返回 ERROR_NOT_FOUND，属于正常竞态（主线程随后会在事件等待里看到关闭）。
    void cancel_server_io() const noexcept
    {
        if (!_server.valid())
            return;
        if (::CancelIoEx(_server.get(), nullptr) != FALSE)
            return;
        const auto err = win32::get_last_error();
        if (err == win32::error::not_found)
            return;
        // 窗口过程不能抛异常（跨 user32 边界是未定义行为），按致命错误处理。
        LOG("pseudo_window: CancelIoEx failed err=%u", static_cast<unsigned>(err));
        std::abort();
    }

    static constexpr const wchar_t *class_name = L"PseudoConsoleWindow";

    win32::event _ready;
    win32::event _close_requested;
    win32::basic_thread _thread;
    std::atomic<HWND> _hwnd{nullptr};
    win32::handle_view _server;
};

} // namespace corehost::conpty
