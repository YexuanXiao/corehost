// tests/lib/conpty_session.hpp
// ConPTY 会话驱动：创建伪控制台、启动真实客户端、后台抽干输出、按条件等待。
//
// 设计见 doc/design/0003：
// - 输出必须由后台线程持续读取，否则管道写满会让被测程序阻塞；
// - 所有等待都是"条件 + 截止时间"，截止时间只用于判定失败；
// - 会话析构时保证子进程、伪控制台、管道全部清理。
#pragma once
#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "libconpty/winconpty.h"
#include "win32/handle.hpp"
#include "win32/srwlock.hpp"
#include "win32/thread.hpp"

namespace corehost::test
{

class conpty_session
{
  public:
    conpty_session() = default;
    ~conpty_session()
    {
        close();
    }

    conpty_session(const conpty_session &) = delete;
    conpty_session &operator=(const conpty_session &) = delete;

    // 创建伪控制台并启动客户端进程（不继承标准句柄，控制台句柄由 ConDrv 提供）。
    bool start(std::wstring_view command_line, COORD size = {80, 25}, DWORD flags = 0)
    {
        HANDLE input_read = nullptr;
        HANDLE input_write = nullptr;
        HANDLE output_read = nullptr;
        HANDLE output_write = nullptr;

        if (!create_inheritable_pipe(input_read, input_write) || !create_inheritable_pipe(output_read, output_write))
        {
            close_partial(input_read, input_write, output_read, output_write);
            return false;
        }

        HPCON pty = nullptr;
        const HRESULT hr = ConptyCreatePseudoConsole(size, input_read, output_write, flags, &pty);
        if (FAILED(hr))
        {
            close_partial(input_read, input_write, output_read, output_write);
            return false;
        }

        // 伪控制台已复制这两个句柄，父进程不再需要它们。
        ::CloseHandle(input_read);
        ::CloseHandle(output_write);

        _pty = pty;
        _input = win32::handle{input_write};
        _output = win32::handle{output_read};

        if (!start_client(command_line))
        {
            close();
            return false;
        }

        _pump = win32::basic_thread{&conpty_session::pump_entry, this, {.name = L"conpty-test-output"}};
        return true;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return _pty != nullptr;
    }

    // 写入终端输入（VT 序列或文本）。
    bool write(std::string_view bytes)
    {
        if (!_input.valid())
            return false;
        DWORD written = 0;
        return ::WriteFile(_input.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
               written == bytes.size();
    }

    // 等待输出流中出现 needle（按字节子串匹配，足够做冒烟断言）。
    bool wait_output(std::string_view needle, DWORD timeout_ms)
    {
        std::unique_lock lock{_mutex};
        return _changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [&] { return _output_text.find(needle) != std::string::npos || _closed; }) &&
               _output_text.find(needle) != std::string::npos;
    }

    // 取走并清空当前累计输出。
    std::string take_output()
    {
        std::unique_lock lock{_mutex};
        std::string result;
        result.swap(_output_text);
        return result;
    }

    bool resize(COORD size)
    {
        if (_pty == nullptr)
            return false;
        return SUCCEEDED(ConptyResizePseudoConsole(_pty, size));
    }

    // 清空可见缓冲区（keep_cursor_row 决定是否保留光标所在行）。
    bool clear(bool keep_cursor_row = false)
    {
        if (_pty == nullptr)
            return false;
        return SUCCEEDED(ConptyClearPseudoConsole(_pty, keep_cursor_row ? TRUE : FALSE));
    }

    // 等待客户端退出；false 表示超时（超时后终止它，避免泄漏）。
    bool wait_exit(DWORD timeout_ms, DWORD *exit_code = nullptr)
    {
        if (!_process.valid())
            return true;

        const DWORD wait = ::WaitForSingleObject(_process.get(), timeout_ms);
        if (wait == WAIT_TIMEOUT)
        {
            ::TerminateProcess(_process.get(), 1);
            ::WaitForSingleObject(_process.get(), INFINITE);
        }

        if (exit_code != nullptr)
            ::GetExitCodeProcess(_process.get(), exit_code);
        return wait != WAIT_TIMEOUT;
    }

    void close()
    {
        // 先结束客户端、关闭伪控制台：corehost 随之退出并关闭 vt_out，输出泵
        // 读到 EOF 自然结束。反过来先 join 泵会死等（CancelSynchronousIo 只能
        // 命中正在进行的同步读，读已返回时就抓不到，join 永不返回）。
        if (_process.valid())
        {
            ::TerminateProcess(_process.get(), 1);
            ::WaitForSingleObject(_process.get(), 5000);
            _process = win32::handle{};
        }

        _input = win32::handle{};

        if (_pty != nullptr)
        {
            ConptyClosePseudoConsole(_pty);
            _pty = nullptr;
        }

        if (_pump.valid())
        {
            ::CancelSynchronousIo(_pump.get()); // 兜底：正常路径上泵已因 EOF 退出
            _pump.join();
        }

        _output = win32::handle{};
    }

  private:
    static bool create_inheritable_pipe(HANDLE &read_end, HANDLE &write_end)
    {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        if (::CreatePipe(&read_end, &write_end, &attributes, 0) == FALSE)
            return false;
        return true;
    }

    static void close_partial(HANDLE a, HANDLE b, HANDLE c, HANDLE d)
    {
        for (HANDLE handle : {a, b, c, d})
        {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                ::CloseHandle(handle);
        }
    }

    bool start_client(std::wstring_view command_line)
    {
        SIZE_T attribute_size = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
        if (attribute_size == 0)
            return false;

        // 属性表要求指针对齐：用 max_align_t 分配，而不是 std::string/vector<byte>。
        _attributes = std::make_unique<std::max_align_t[]>(
            (attribute_size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_attributes.get());
        if (::InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) == FALSE)
            return false;

        if (::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, _pty, sizeof(_pty), nullptr,
                                        nullptr) == FALSE)
        {
            ::DeleteProcThreadAttributeList(attributes);
            return false;
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        // 与 bench/conpty_session.hpp 的可用配方一致：标准句柄留空（控制台句柄
        // 由 ConDrv 提供），不继承父进程句柄。
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

        PROCESS_INFORMATION process{};
        std::wstring mutable_command{command_line};
        const BOOL created = ::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                                              EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                              nullptr, &startup.StartupInfo, &process);

        ::DeleteProcThreadAttributeList(attributes);

        if (created == FALSE)
            return false;

        _process = win32::handle{process.hProcess};
        ::CloseHandle(process.hThread);

        // 关键一步：释放 \Reference，客户端才能完成控制台连接（bench 同款）。
        if (FAILED(ConptyReleasePseudoConsole(_pty)))
            return false;
        return true;
    }

    static DWORD WINAPI pump_entry(LPVOID self)
    {
        static_cast<conpty_session *>(self)->pump();
        return 0;
    }

    // 后台抽干输出管道：不读就会让客户端因管道写满而阻塞。
    void pump()
    {
        std::array<char, 4096> buffer{};
        for (;;)
        {
            DWORD read = 0;
            const BOOL ok = ::ReadFile(_output.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
            if (ok == FALSE || read == 0)
                break;

            {
                std::unique_lock lock{_mutex};
                _output_text.append(buffer.data(), read);
            }
            _changed.notify_all();
        }

        {
            std::unique_lock lock{_mutex};
            _closed = true;
        }
        _changed.notify_all();
    }

    HPCON _pty = nullptr;
    win32::handle _input;
    win32::handle _output;
    win32::handle _process;
    HANDLE _job = nullptr;
    win32::basic_thread _pump;
    std::unique_ptr<std::max_align_t[]> _attributes;

    win32::shared_mutex _mutex;
    win32::condition_variable _changed;
    std::string _output_text;
    bool _closed = false;
};

} // namespace corehost::test
