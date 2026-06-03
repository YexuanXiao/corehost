#pragma once

// Minimal ConPTY session harness used by each scenario. It owns the HPCON, the
// child process attached to that HPCON, a write handle for terminal input, and a
// background reader for terminal output.

#include "common.hpp"
#include "libconpty/libconpty.hpp"
#include "win32/wait.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace bench
{

// Background reader for the PTY output pipe. It counts all bytes for throughput
// and keeps a small rolling tail so wait_for* can detect scenario markers.
class pty_reader
{
  public:
    explicit pty_reader(win32::handle output) : _output{std::move(output)}
    {
        _thread = std::thread([this] { read_loop(); });
    }

    pty_reader(const pty_reader &) = delete;
    pty_reader &operator=(const pty_reader &) = delete;

    ~pty_reader()
    {
        join();
    }

    [[nodiscard]] size_t bytes_read() const
    {
        std::scoped_lock lock{_mutex};
        return _bytes_read;
    }

    bool wait_for(std::string_view marker, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{_mutex};
        return _cv.wait_until(lock, deadline, [&] { return _tail.find(marker) != std::string::npos || _closed; }) &&
               _tail.find(marker) != std::string::npos;
    }

    bool wait_for_after(std::string_view marker, size_t minimum_offset, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{_mutex};
        return _cv.wait_until(lock, deadline, [&] { return contains_after(marker, minimum_offset) || _closed; }) &&
               contains_after(marker, minimum_offset);
    }

    void join()
    {
        if (_thread.joinable())
            _thread.join();
    }

    void cancel()
    {
        if (_thread.joinable())
            ::CancelSynchronousIo(_thread.native_handle());
        _output.clear();
    }

  private:
    void read_loop()
    {
        char buffer[64 * 1024];
        for (;;)
        {
            DWORD read = 0;
            if (!::ReadFile(_output.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0)
                break;

            {
                std::scoped_lock lock{_mutex};
                _bytes_read += read;
                if (read >= max_tail_bytes)
                {
                    _tail.assign(buffer + read - max_tail_bytes, max_tail_bytes);
                }
                else
                {
                    const auto overflow =
                        _tail.size() + read > max_tail_bytes ? _tail.size() + read - max_tail_bytes : 0;
                    if (overflow != 0)
                        _tail.erase(0, overflow);
                    _tail.append(buffer, read);
                }
            }
            _cv.notify_all();
        }

        {
            std::scoped_lock lock{_mutex};
            _closed = true;
        }
        _cv.notify_all();
    }

    [[nodiscard]] bool contains_after(std::string_view marker, size_t minimum_offset) const
    {
        const size_t tail_begin = _bytes_read >= _tail.size() ? _bytes_read - _tail.size() : 0;
        const size_t search_begin = minimum_offset > tail_begin ? minimum_offset - tail_begin : 0;
        return search_begin <= _tail.size() && _tail.find(marker, search_begin) != std::string::npos;
    }

    static constexpr size_t max_tail_bytes = 8 * 1024;

    win32::handle _output;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::string _tail;
    size_t _bytes_read = 0;
    bool _closed = false;
};

// Creates one pseudoconsole and runs one child command inside it. Scenarios use
// write() to send terminal input, wait_for*() to observe worker markers, and
// bytes_read() to measure host output volume.
class conpty_session
{
  public:
    explicit conpty_session(std::wstring child_command = L"cmd.exe /d /q")
    {
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};

        win32::handle pty_input_read;
        win32::handle pty_output_write;
        if (!::CreatePipe(pty_input_read.put(), _input_write.put(), &inheritable, 0))
        {
            print_and_abort("CreatePipe(input) failed: %lu\n", ::GetLastError());
        }
        if (!::CreatePipe(_output_read.put(), pty_output_write.put(), &inheritable, 0))
        {
            print_and_abort("CreatePipe(output) failed: %lu\n", ::GetLastError());
        }

        constexpr COORD size{240, 40};
        if (const HRESULT hr = ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(), 0, &_hpc);
            FAILED(hr))
        {
            print_and_abort("ConptyCreatePseudoConsole failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        }

        pty_input_read.clear();
        pty_output_write.clear();

        start_child(std::move(child_command));
        _reader = std::make_unique<pty_reader>(std::move(_output_read));
    }

    conpty_session(const conpty_session &) = delete;
    conpty_session &operator=(const conpty_session &) = delete;

    ~conpty_session()
    {
        stop();
    }

    void write(std::string_view bytes)
    {
        write_all(_input_write.get(), bytes);
    }

    bool write_for(std::string_view bytes, std::chrono::milliseconds timeout)
    {
        struct write_state
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool done = false;
            DWORD error = ERROR_SUCCESS;
        };

        write_state state;
        std::thread writer([&] {
            auto remaining = bytes;
            while (!remaining.empty())
            {
                DWORD written = 0;
                const auto chunk = static_cast<DWORD>(std::min<size_t>(remaining.size(), 4 * 1024));
                if (!::WriteFile(_input_write.get(), remaining.data(), chunk, &written, nullptr))
                {
                    std::scoped_lock lock{state.mutex};
                    state.error = ::GetLastError();
                    state.done = true;
                    state.cv.notify_all();
                    return;
                }
                if (written == 0)
                {
                    std::scoped_lock lock{state.mutex};
                    state.error = ERROR_WRITE_FAULT;
                    state.done = true;
                    state.cv.notify_all();
                    return;
                }
                remaining.remove_prefix(written);
            }

            std::scoped_lock lock{state.mutex};
            state.done = true;
            state.cv.notify_all();
        });

        bool completed = false;
        DWORD error = ERROR_SUCCESS;
        {
            std::unique_lock lock{state.mutex};
            completed = state.cv.wait_for(lock, timeout, [&] { return state.done; });
            error = state.error;
        }

        if (!completed)
        {
            ::CancelSynchronousIo(writer.native_handle());
            _input_write.clear();
        }
        writer.join();
        return completed && error == ERROR_SUCCESS;
    }

    [[nodiscard]] size_t bytes_read() const
    {
        return _reader->bytes_read();
    }

    bool wait_for(std::string_view marker, std::chrono::milliseconds timeout)
    {
        return _reader->wait_for(marker, timeout);
    }

    bool wait_for_after(std::string_view marker, size_t minimum_offset, std::chrono::milliseconds timeout)
    {
        return _reader->wait_for_after(marker, minimum_offset, timeout);
    }

    void stop()
    {
        if (_stopped)
            return;
        _stopped = true;

        if (_process.valid())
        {
            ::TerminateProcess(_process.get(), 0);
            try
            {
                const auto wait = win32::wait_one(_process, 3000);
                if (wait.abandoned())
                    print_and_abort("child process wait abandoned\n");
            }
            catch (win32::error err)
            {
                print_and_abort("child process wait failed: %u\n", static_cast<unsigned>(err));
            }
        }

        if (_hpc)
        {
            ConptyClosePseudoConsole(_hpc);
            _hpc = nullptr;
        }

        _input_write.clear();
        if (_reader)
        {
            _reader->cancel();
            _reader->join();
        }
    }

  private:
    void start_child(std::wstring command)
    {
        console::proc_thread_attribute_list attributes;
        if (const HRESULT hr = attributes.initialize(1); FAILED(hr))
        {
            print_and_abort("InitializeProcThreadAttributeList failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        }
        if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, _hpc, sizeof(_hpc),
                                         nullptr, nullptr))
        {
            print_and_abort("UpdateProcThreadAttribute(PSEUDOCONSOLE) failed: %lu\n", ::GetLastError());
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.lpAttributeList = attributes.get();

        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                              &startup.StartupInfo, &pi))
        {
            print_and_abort("CreateProcessW(cmd) failed: %lu\n", ::GetLastError());
        }

        _process = win32::handle{pi.hProcess};
        ::CloseHandle(pi.hThread);

        if (const HRESULT hr = ConptyReleasePseudoConsole(_hpc); FAILED(hr))
        {
            print_and_abort("ConptyReleasePseudoConsole failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        }
    }

    HPCON _hpc = nullptr;
    win32::handle _input_write;
    win32::handle _output_read;
    win32::handle _process;
    std::unique_ptr<pty_reader> _reader;
    bool _stopped = false;
};

} // namespace bench
