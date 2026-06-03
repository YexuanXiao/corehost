#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#include "libconpty/libconpty.hpp"
#include "win32/handle.hpp"
#include "win32/wait.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::literals;

namespace
{

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

[[noreturn]] void fail_win32(const char *message)
{
    std::fprintf(stderr, "%s: %lu\n", message, ::GetLastError());
    std::exit(1);
}

std::wstring getenv_wide(const wchar_t *name)
{
    const DWORD required = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};
    std::wstring value(required - 1, L'\0');
    ::GetEnvironmentVariableW(name, value.data(), required);
    return value;
}

bool file_exists(const std::wstring &path)
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring find_edit_exe()
{
    if (auto env = getenv_wide(L"COREHOST_EDIT_EXE"); !env.empty())
        return env;

    wchar_t system_dir[MAX_PATH]{};
    const UINT system_len = ::GetSystemDirectoryW(system_dir, MAX_PATH);
    if (system_len != 0 && system_len < MAX_PATH)
    {
        std::wstring path{system_dir, system_len};
        path += L"\\edit.exe";
        if (file_exists(path))
            return path;
    }

    wchar_t found[MAX_PATH]{};
    if (::SearchPathW(nullptr, L"edit.exe", nullptr, MAX_PATH, found, nullptr) != 0)
        return found;

    return {};
}

std::wstring quote(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    result.append(value);
    result.push_back(L'"');
    return result;
}

void write_all(HANDLE pipe, std::string_view bytes)
{
    while (!bytes.empty())
    {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), 4096));
        if (!::WriteFile(pipe, bytes.data(), chunk, &written, nullptr))
            fail_win32("WriteFile(terminal input) failed");
        if (written == 0)
            fail("WriteFile(terminal input) wrote zero bytes");
        bytes.remove_prefix(written);
    }
}

std::string win32_key_sequence(WORD vk, WORD scan, WCHAR ch, bool key_down, DWORD control_state = 0,
                               WORD repeat_count = 1)
{
    char buffer[96];
    const int length = std::snprintf(buffer, sizeof(buffer), "\x1b[%u;%u;%u;%u;%lu;%u_", static_cast<unsigned>(vk),
                                     static_cast<unsigned>(scan), static_cast<unsigned>(ch), key_down ? 1u : 0u,
                                     static_cast<unsigned long>(control_state), static_cast<unsigned>(repeat_count));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(buffer))
        fail("failed to format Win32 input sequence");
    return {buffer, static_cast<size_t>(length)};
}

class terminal_reader
{
  public:
    terminal_reader(win32::handle output_read, HANDLE input_write)
        : _output_read{std::move(output_read)}, _input_write{input_write}
    {
        _thread = std::thread([this] { read_loop(); });
    }

    ~terminal_reader()
    {
        cancel();
        join();
    }

    terminal_reader(const terminal_reader &) = delete;
    terminal_reader &operator=(const terminal_reader &) = delete;

    bool wait_for(std::string_view marker, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{_mutex};
        return _cv.wait_until(lock, deadline, [&] { return _output.find(marker) != std::string::npos || _closed; }) &&
               _output.find(marker) != std::string::npos;
    }

    bool wait_for_any(std::span<const std::string_view> markers, std::chrono::milliseconds timeout)
    {
        const auto contains_marker = [&] {
            return std::ranges::any_of(
                markers, [&](std::string_view marker) { return _output.find(marker) != std::string::npos; });
        };
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock{_mutex};
        return _cv.wait_until(lock, deadline, [&] { return contains_marker() || _closed; }) && contains_marker();
    }

    std::string snapshot()
    {
        std::scoped_lock lock{_mutex};
        return _output;
    }

    void cancel()
    {
        if (_thread.joinable())
            ::CancelSynchronousIo(_thread.native_handle());
        _output_read.clear();
    }

    void join()
    {
        if (_thread.joinable())
            _thread.join();
    }

  private:
    void append_output(const char *data, DWORD size)
    {
        {
            std::scoped_lock lock{_mutex};
            _output.append(data, size);
            if (_output.size() > max_capture)
                _output.erase(0, _output.size() - max_capture);
        }
        _cv.notify_all();
    }

    void respond_to_osc_color_queries(std::string_view chunk)
    {
        if (chunk.find("\x1b]10;?"sv) != std::string_view::npos)
            write_all(_input_write, "\x1b]10;rgb:eeee/eeee/eeee\x1b\\"sv);
        if (chunk.find("\x1b]11;?"sv) != std::string_view::npos)
            write_all(_input_write, "\x1b]11;rgb:1111/1111/1111\x1b\\"sv);
        if (chunk.find("\x1b]4;"sv) != std::string_view::npos)
        {
            write_all(_input_write, "\x1b]4;0;rgb:0000/0000/0000;1;rgb:cdcd/3131/3131;2;rgb:0e0e/a1a1/1313;"
                                    "3;rgb:e5e5/e5e5/1010;4;rgb:2424/7171/c8c8;5;rgb:bcbc/3f3f/bcbc;"
                                    "6;rgb:1111/a8a8/cdcd;7;rgb:e5e5/e5e5/e5e5\x1b\\"
                                    "\x1b]4;8;rgb:6666/6666/6666;9;rgb:f1f1/4c4c/4c4c;10;rgb:2323/d1d1/8b8b;"
                                    "11;rgb:f5f5/f5f5/4343;12;rgb:3b3b/8e8e/eaea;13;rgb:d6d6/7070/d6d6;"
                                    "14;rgb:2929/b8b8/dbdb;15;rgb:eeee/eeee/eeee\x1b\\"sv);
        }
    }

    void read_loop()
    {
        char buffer[4096];
        for (;;)
        {
            DWORD read = 0;
            if (!::ReadFile(_output_read.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0)
                break;
            append_output(buffer, read);
            respond_to_osc_color_queries({buffer, read});
        }

        {
            std::scoped_lock lock{_mutex};
            _closed = true;
        }
        _cv.notify_all();
    }

    static constexpr size_t max_capture = 1024 * 1024;

    win32::handle _output_read;
    HANDLE _input_write = nullptr;
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::string _output;
    bool _closed = false;
};

struct conpty_edit_session
{
    HPCON hpc = nullptr;
    win32::handle input_write;
    win32::handle output_read;
    win32::handle child_process;
    std::unique_ptr<terminal_reader> reader;

    ~conpty_edit_session()
    {
        stop();
    }

    void start(const std::wstring &edit_path)
    {
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
        win32::handle pty_input_read;
        win32::handle pty_output_write;
        if (!::CreatePipe(pty_input_read.put(), input_write.put(), &inheritable, 0))
            fail_win32("CreatePipe(input) failed");
        if (!::CreatePipe(output_read.put(), pty_output_write.put(), &inheritable, 0))
            fail_win32("CreatePipe(output) failed");

        constexpr COORD size{120, 30};
        const HRESULT hr = ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(), 0, &hpc);
        if (FAILED(hr))
        {
            std::fprintf(stderr, "ConptyCreatePseudoConsole failed: 0x%08lx\n", static_cast<unsigned long>(hr));
            std::exit(1);
        }

        pty_input_read.clear();
        pty_output_write.clear();

        console::proc_thread_attribute_list attributes;
        if (FAILED(attributes.initialize(1)))
            fail("InitializeProcThreadAttributeList failed");
        if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc),
                                         nullptr, nullptr))
            fail_win32("UpdateProcThreadAttribute(PSEUDOCONSOLE) failed");

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.lpAttributeList = attributes.get();

        auto command = quote(edit_path);
        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                              &startup.StartupInfo, &pi))
            fail_win32("CreateProcessW(edit.exe) failed");

        child_process = win32::handle{pi.hProcess};
        ::CloseHandle(pi.hThread);

        if (FAILED(ConptyReleasePseudoConsole(hpc)))
            fail("ConptyReleasePseudoConsole failed");

        reader = std::make_unique<terminal_reader>(std::move(output_read), input_write.get());
    }

    bool wait_for_exit(std::chrono::milliseconds timeout)
    {
        try
        {
            const auto wait = win32::wait_one(child_process, static_cast<DWORD>(timeout.count()));
            if (wait.abandoned())
                fail("Wait for edit.exe exit was abandoned");
            return wait.signaled();
        }
        catch (win32::error err)
        {
            std::fprintf(stderr, "Wait for edit.exe exit failed: %u\n", static_cast<unsigned>(err));
            std::exit(1);
        }
    }

    void send_ctrl_q()
    {
        send_key(0x51, 16, 0x11, true, LEFT_CTRL_PRESSED);
        send_key(0x51, 16, 0, false);
    }

    void send_key(WORD vk, WORD scan, WCHAR ch, bool key_down, DWORD control_state = 0)
    {
        const auto sequence = win32_key_sequence(vk, scan, ch, key_down, control_state);
        write_all(input_write.get(), sequence);
    }

    void send_text(std::string_view text)
    {
        for (const unsigned char ch : text)
        {
            if (ch < 0x20 || ch >= 0x7f)
                fail("send_text only supports printable ASCII");
            const SHORT vk_scan = ::VkKeyScanA(static_cast<char>(ch));
            if (vk_scan == -1)
                fail("VkKeyScanA failed");
            const auto vk = static_cast<WORD>(vk_scan & 0xff);
            const auto scan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
            const DWORD control = (vk_scan & 0x0100) ? SHIFT_PRESSED : 0;
            send_key(vk, scan, static_cast<WCHAR>(ch), true, control);
            send_key(vk, scan, 0, false, control);
        }
    }

    void send_discard_unsaved()
    {
        const auto scan = static_cast<WORD>(::MapVirtualKeyW('N', MAPVK_VK_TO_VSC));
        send_key('N', scan, L'n', true);
        send_key('N', scan, 0, false);
    }

    void stop()
    {
        if (reader)
        {
            reader->cancel();
            reader->join();
            reader.reset();
        }
        input_write.clear();
        if (hpc)
        {
            ConptyClosePseudoConsole(hpc);
            hpc = nullptr;
        }
        bool child_exited = true;
        if (child_process.valid())
        {
            try
            {
                const auto wait = win32::wait_one(child_process, 1000);
                if (wait.abandoned())
                    fail("Wait for edit.exe shutdown was abandoned");
                child_exited = wait.signaled();
            }
            catch (win32::error err)
            {
                std::fprintf(stderr, "Wait for edit.exe shutdown failed: %u\n", static_cast<unsigned>(err));
                std::exit(1);
            }
        }
        if (child_process.valid() && !child_exited)
        {
            ::TerminateProcess(child_process.get(), 1);
            try
            {
                const auto wait = win32::wait_one(child_process, 3000);
                if (wait.abandoned())
                    fail("Wait for terminated edit.exe was abandoned");
            }
            catch (win32::error err)
            {
                std::fprintf(stderr, "Wait for terminated edit.exe failed: %u\n", static_cast<unsigned>(err));
                std::exit(1);
            }
        }
    }
};

} // namespace

int main()
{
    auto edit_path = find_edit_exe();
    if (edit_path.empty() || !file_exists(edit_path))
    {
        std::fprintf(stderr, "SKIP: edit.exe was not found\n");
        return 77;
    }

    std::fwprintf(stderr, L"Using edit: %ls\n", edit_path.c_str());

    conpty_edit_session session;
    session.start(edit_path);

    if (!session.reader->wait_for("\x1b[?9001h"sv, std::chrono::seconds{5}))
    {
        std::fprintf(stderr, "corehost did not enable Win32 input mode\n");
        std::fwrite(session.reader->snapshot().data(), 1, session.reader->snapshot().size(), stderr);
        return 1;
    }

    if (!session.reader->wait_for("\x1b[?1049h"sv, std::chrono::seconds{10}))
    {
        std::fprintf(stderr, "edit did not enter alternate screen\n");
        auto output = session.reader->snapshot();
        std::fwrite(output.data(), 1, output.size(), stderr);
        return 1;
    }

    if (!session.reader->wait_for("\x1b]0;"sv, std::chrono::seconds{10}))
    {
        std::fprintf(stderr, "edit did not render its first UI frame\n");
        auto output = session.reader->snapshot();
        std::fwrite(output.data(), 1, output.size(), stderr);
        return 1;
    }

    constexpr auto edit_text = "corehost-edit-smoke-9371"sv;
    session.send_text(edit_text);
    if (!session.reader->wait_for(edit_text, std::chrono::seconds{10}))
    {
        std::fprintf(stderr, "edit did not render typed text\n");
        auto output = session.reader->snapshot();
        std::fwrite(output.data(), 1, output.size(), stderr);
        return 1;
    }

    session.send_ctrl_q();
    constexpr std::string_view unsaved_markers[] = {
        "Unsaved changes"sv,
        "\xE6\x9C\xAA\xE4\xBF\x9D\xE5\xAD\x98\xE7\x9A\x84\xE6\x9B\xB4\xE6\x94\xB9"sv,
    };
    if (!session.reader->wait_for_any(unsaved_markers, std::chrono::seconds{10}))
    {
        std::fprintf(stderr, "edit did not show unsaved changes dialog after editing\n");
        auto output = session.reader->snapshot();
        std::fwrite(output.data(), 1, output.size(), stderr);
        return 1;
    }

    session.send_discard_unsaved();
    if (!session.wait_for_exit(std::chrono::seconds{10}))
    {
        std::fprintf(stderr, "edit did not exit after Ctrl+Q\n");
        auto output = session.reader->snapshot();
        std::fwrite(output.data(), 1, output.size(), stderr);
        return 1;
    }

    return 0;
}
