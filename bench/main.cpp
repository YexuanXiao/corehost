#include "libconpty/libconpty.hpp"

#include "win32/handle.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace bench
{

struct win32_failure
{
    const char *operation;
    DWORD error;
};

struct hresult_failure
{
    const char *operation;
    HRESULT hr;
};

struct scenario_result
{
    std::string host;
    std::string name;
    size_t input_bytes = 0;
    size_t output_bytes = 0;
    double elapsed_ms = 0;
};

struct host_result
{
    std::wstring source_path;
    std::string label;
    std::vector<scenario_result> scenarios;
};

[[nodiscard]] int64_t perf_counter()
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return value.QuadPart;
}

[[nodiscard]] int64_t perf_frequency()
{
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart;
}

[[nodiscard]] double elapsed_ms(int64_t begin, int64_t end)
{
    return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(perf_frequency());
}

void throw_last_error(const char *operation)
{
    throw win32_failure{operation, ::GetLastError()};
}

void throw_if_failed(HRESULT hr, const char *operation)
{
    if (FAILED(hr))
        throw hresult_failure{operation, hr};
}

[[nodiscard]] std::string narrow(std::wstring_view value)
{
    if (value.empty())
        return {};

    const int required =
        ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        throw_last_error("WideCharToMultiByte(size)");

    std::string result(static_cast<size_t>(required), '\0');
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                                              required, nullptr, nullptr);
    if (written != required)
        throw_last_error("WideCharToMultiByte(data)");
    return result;
}

[[nodiscard]] std::wstring widen(std::string_view value)
{
    if (value.empty())
        return {};

    const int required = ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
        throw_last_error("MultiByteToWideChar(size)");

    std::wstring result(static_cast<size_t>(required), L'\0');
    const int written =
        ::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    if (written != required)
        throw_last_error("MultiByteToWideChar(data)");
    return result;
}

void write_all(HANDLE pipe, std::string_view bytes)
{
    while (!bytes.empty())
    {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), 64 * 1024));
        if (!::WriteFile(pipe, bytes.data(), chunk, &written, nullptr))
            throw_last_error("WriteFile(pty input)");
        if (written == 0)
            throw win32_failure{"WriteFile(pty input zero bytes)", ERROR_BROKEN_PIPE};
        bytes.remove_prefix(written);
    }
}

[[nodiscard]] std::string encode_terminal_input(std::string_view utf8)
{
    return std::string{utf8};
}

class proc_thread_attribute_list
{
  public:
    explicit proc_thread_attribute_list(DWORD count)
    {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        _list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, size));
        if (!_list)
            throw hresult_failure{"HeapAlloc(attribute list)", E_OUTOFMEMORY};
        if (!::InitializeProcThreadAttributeList(_list, count, 0, &size))
            throw_last_error("InitializeProcThreadAttributeList");
    }

    proc_thread_attribute_list(const proc_thread_attribute_list &) = delete;
    proc_thread_attribute_list &operator=(const proc_thread_attribute_list &) = delete;

    ~proc_thread_attribute_list()
    {
        if (_list)
        {
            ::DeleteProcThreadAttributeList(_list);
            ::HeapFree(::GetProcessHeap(), 0, _list);
        }
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept
    {
        return _list;
    }

  private:
    LPPROC_THREAD_ATTRIBUTE_LIST _list = nullptr;
};

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

    [[nodiscard]] std::string tail_snapshot() const
    {
        std::scoped_lock lock{_mutex};
        return _tail;
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
                _tail.append(buffer, read);
                if (_tail.size() > max_tail_bytes)
                    _tail.erase(0, _tail.size() - max_tail_bytes);
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

    static constexpr size_t max_tail_bytes = 1024 * 1024;

    win32::handle _output;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::string _tail;
    size_t _bytes_read = 0;
    bool _closed = false;
};

class conpty_session
{
  public:
    conpty_session()
    {
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};

        win32::handle pty_input_read;
        win32::handle pty_output_write;
        if (!::CreatePipe(pty_input_read.put(), _input_write.put(), &inheritable, 0))
            throw_last_error("CreatePipe(input)");
        if (!::CreatePipe(_output_read.put(), pty_output_write.put(), &inheritable, 0))
            throw_last_error("CreatePipe(output)");

        constexpr COORD size{240, 40};
        throw_if_failed(ConptyCreatePseudoConsole(size, pty_input_read.get(), pty_output_write.get(), 0, &_hpc),
                        "ConptyCreatePseudoConsole");

        pty_input_read.clear();
        pty_output_write.clear();

        start_child();
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

    [[nodiscard]] std::string output_tail() const
    {
        return _reader->tail_snapshot();
    }

    void stop()
    {
        if (_stopped)
            return;
        _stopped = true;

        if (_process.valid())
        {
            ::TerminateProcess(_process.get(), 0);
            ::WaitForSingleObject(_process.get(), 3000);
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
    void start_child()
    {
        proc_thread_attribute_list attributes{1};
        if (!::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, _hpc, sizeof(_hpc),
                                         nullptr, nullptr))
            throw_last_error("UpdateProcThreadAttribute(PSEUDOCONSOLE)");

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.lpAttributeList = attributes.get();

        std::wstring command = L"cmd.exe /d /q";
        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                              &startup.StartupInfo, &pi))
            throw_last_error("CreateProcessW(cmd)");

        _process = win32::handle{pi.hProcess};
        ::CloseHandle(pi.hThread);

        throw_if_failed(ConptyReleasePseudoConsole(_hpc), "ConptyReleasePseudoConsole");
    }

    HPCON _hpc = nullptr;
    win32::handle _input_write;
    win32::handle _output_read;
    win32::handle _process;
    std::unique_ptr<pty_reader> _reader;
    bool _stopped = false;
};

scenario_result run_payload(conpty_session &session, std::string host, std::string name, std::string payload,
                            std::chrono::milliseconds timeout)
{
    constexpr std::string_view prompt_marker = "__CONPTY_BENCH_PROMPT__>";

    auto encoded_payload = encode_terminal_input(payload);

    const size_t before = session.bytes_read();
    const int64_t begin = perf_counter();
    session.write(encoded_payload);
    if (!session.wait_for_after(prompt_marker, before, timeout))
    {
        auto tail = session.output_tail();
        if (tail.size() > 4096)
            tail.erase(0, tail.size() - 4096);
        std::fwrite(tail.data(), 1, tail.size(), stderr);
        throw win32_failure{"wait for scenario prompt", WAIT_TIMEOUT};
    }
    const int64_t end = perf_counter();
    const size_t after = session.bytes_read();

    return scenario_result{
        .host = std::move(host),
        .name = std::move(name),
        .input_bytes = encoded_payload.size(),
        .output_bytes = after - before,
        .elapsed_ms = elapsed_ms(begin, end),
    };
}

scenario_result run_payload_sequence(conpty_session &session, std::string host, std::string name,
                                     const std::vector<std::string> &payloads, std::chrono::milliseconds timeout)
{
    constexpr std::string_view prompt_marker = "__CONPTY_BENCH_PROMPT__>";

    size_t input_bytes = 0;
    const size_t before = session.bytes_read();
    const int64_t begin = perf_counter();

    for (const auto &payload : payloads)
    {
        auto encoded_payload = encode_terminal_input(payload);
        input_bytes += encoded_payload.size();

        const size_t command_before = session.bytes_read();
        session.write(encoded_payload);
        if (!session.wait_for_after(prompt_marker, command_before, timeout))
        {
            auto tail = session.output_tail();
            if (tail.size() > 4096)
                tail.erase(0, tail.size() - 4096);
            std::fwrite(tail.data(), 1, tail.size(), stderr);
            throw win32_failure{"wait for scenario prompt", WAIT_TIMEOUT};
        }
    }

    const int64_t end = perf_counter();
    const size_t after = session.bytes_read();

    return scenario_result{
        .host = std::move(host),
        .name = std::move(name),
        .input_bytes = input_bytes,
        .output_bytes = after - before,
        .elapsed_ms = elapsed_ms(begin, end),
    };
}

[[nodiscard]] std::string repeated_text(std::string_view text, size_t count)
{
    std::string result;
    result.reserve(text.size() * count);
    for (size_t i = 0; i < count; ++i)
    {
        result.append(text);
    }
    return result;
}

[[nodiscard]] std::vector<std::string> repeated_commands(std::string_view command, size_t count)
{
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.emplace_back(command);
    return result;
}

void print_worker_results(const std::vector<scenario_result> &results);

std::vector<scenario_result> run_worker_bench(std::string host_label)
{
    conpty_session session;
    if (!session.wait_for(">", std::chrono::seconds{10}))
        throw win32_failure{"wait for initial prompt", WAIT_TIMEOUT};

    session.write(encode_terminal_input("prompt __CONPTY_BENCH_PROMPT__$G\r"));
    if (!session.wait_for("__CONPTY_BENCH_PROMPT__>", std::chrono::seconds{10}))
        throw win32_failure{"wait for benchmark prompt", WAIT_TIMEOUT};

    std::vector<scenario_result> results;
    results.push_back(run_payload(session, host_label, "init", "chcp 65001 > nul\r", std::chrono::seconds{10}));

    results.push_back(run_payload(session, host_label, "bulk-ascii-output",
                                  "for /l %i in (1,1,512) do echo abcdefghijklmnopqrstuvwxyz0123456789\r",
                                  std::chrono::seconds{30}));

    results.push_back(run_payload(
        session, host_label, "many-small-input",
        "rem " + repeated_text("bench input line 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ", 8) +
            "\r",
        std::chrono::seconds{30}));

    results.push_back(run_payload(session, host_label, "echo-command-input",
                                  "echo " + repeated_text("input 0123456789 ", 16) + "\r", std::chrono::seconds{30}));

    results.push_back(
        run_payload(session, host_label, "unicode-echo", "echo 喜欢你 corehost conpty\r", std::chrono::seconds{30}));

    results.push_back(run_payload_sequence(session, host_label, "prompt-roundtrip", repeated_commands("rem rt\r", 16),
                                           std::chrono::seconds{10}));

    results.push_back(run_payload(session, host_label, "large-ascii-output",
                                  "for /l %i in (1,1,2048) do echo abcdefghijklmnopqrstuvwxyz0123456789\r",
                                  std::chrono::seconds{120}));

    session.stop();
    return results;
}

void print_worker_results(const std::vector<scenario_result> &results)
{
    for (const auto &scenario : results)
    {
        std::printf("RESULT\t%s\t%s\t%zu\t%zu\t%.6f\n", scenario.host.c_str(), scenario.name.c_str(),
                    scenario.input_bytes, scenario.output_bytes, scenario.elapsed_ms);
    }
}

[[nodiscard]] std::vector<std::string_view> split_tabs(std::string_view line)
{
    std::vector<std::string_view> parts;
    while (!line.empty())
    {
        const size_t pos = line.find('\t');
        parts.push_back(line.substr(0, pos));
        if (pos == std::string_view::npos)
            break;
        line.remove_prefix(pos + 1);
    }
    return parts;
}

[[nodiscard]] std::vector<scenario_result> parse_worker_results(std::string_view output)
{
    std::vector<scenario_result> results;
    while (!output.empty())
    {
        size_t end = output.find('\n');
        auto line = output.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        const auto parts = split_tabs(line);
        if (parts.size() == 6 && parts[0] == "RESULT")
        {
            results.push_back(scenario_result{
                .host = std::string{parts[1]},
                .name = std::string{parts[2]},
                .input_bytes = static_cast<size_t>(std::stoull(std::string{parts[3]})),
                .output_bytes = static_cast<size_t>(std::stoull(std::string{parts[4]})),
                .elapsed_ms = std::stod(std::string{parts[5]}),
            });
        }

        if (end == std::string_view::npos)
            break;
        output.remove_prefix(end + 1);
    }
    return results;
}

[[nodiscard]] std::wstring current_exe_path()
{
    std::wstring path;
    path.resize_and_overwrite(32768, [](wchar_t *buffer, size_t capacity) noexcept {
        return static_cast<size_t>(::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(capacity)));
    });
    return path;
}

[[nodiscard]] std::filesystem::path make_worker_dir(size_t index)
{
    std::wstring temp;
    temp.resize(MAX_PATH);
    const DWORD length = ::GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length > temp.size())
        throw_last_error("GetTempPathW");
    temp.resize(length);

    auto dir = std::filesystem::path{temp} /
               (L"corehost-conpty-bench-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(index));
    std::filesystem::create_directories(dir);
    return dir;
}

void copy_host_tree(const std::filesystem::path &host_path, const std::filesystem::path &worker_dir)
{
    std::filesystem::copy_file(host_path, worker_dir / L"corehost.exe",
                               std::filesystem::copy_options::overwrite_existing);

    const auto source_dir = host_path.parent_path();
    if (!source_dir.empty())
    {
        for (const auto &entry : std::filesystem::directory_iterator{source_dir})
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() == L".dll")
            {
                std::filesystem::copy_file(entry.path(), worker_dir / entry.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
}

[[nodiscard]] std::wstring quote(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    result.append(value);
    result.push_back(L'"');
    return result;
}

[[nodiscard]] std::string run_worker_process(const std::filesystem::path &worker_exe,
                                             const std::filesystem::path &worker_dir, std::string_view label)
{
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    win32::handle read_pipe;
    win32::handle write_pipe;
    if (!::CreatePipe(read_pipe.put(), write_pipe.put(), &inheritable, 0))
        throw_last_error("CreatePipe(worker stdout)");
    if (!::SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0))
        throw_last_error("SetHandleInformation(worker stdout)");

    const auto wide_label = widen(label);
    auto command = quote(worker_exe.wstring()) + L" --worker " + quote(wide_label);

    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe.get();
    startup.hStdError = write_pipe.get();

    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                          worker_dir.c_str(), &startup, &pi))
        throw_last_error("CreateProcessW(worker)");

    win32::handle process{pi.hProcess};
    ::CloseHandle(pi.hThread);
    write_pipe.clear();

    std::string output;
    char buffer[64 * 1024];
    for (;;)
    {
        DWORD read = 0;
        if (!::ReadFile(read_pipe.get(), buffer, sizeof(buffer), &read, nullptr) || read == 0)
            break;
        output.append(buffer, read);
    }

    ::WaitForSingleObject(process.get(), INFINITE);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.get(), &exit_code);
    if (exit_code != 0)
    {
        std::fwrite(output.data(), 1, output.size(), stderr);
        throw win32_failure{"worker process", exit_code};
    }
    return output;
}

[[nodiscard]] std::string label_from_path(std::wstring_view path)
{
    return narrow(std::filesystem::path{path}.filename().wstring());
}

void validate_host_path(const wchar_t *path)
{
    const DWORD attributes = ::GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw win32_failure{"invalid host path",
                            attributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_DIRECTORY};
}

host_result run_host_via_worker(const wchar_t *host_path, std::string label, size_t index)
{
    const auto worker_dir = make_worker_dir(index);
    const auto worker_exe = worker_dir / L"conpty_bench_worker.exe";

    std::filesystem::copy_file(current_exe_path(), worker_exe, std::filesystem::copy_options::overwrite_existing);
    copy_host_tree(host_path, worker_dir);

    auto output = run_worker_process(worker_exe, worker_dir, label);
    auto scenarios = parse_worker_results(output);
    if (scenarios.empty())
    {
        std::fwrite(output.data(), 1, output.size(), stderr);
        throw win32_failure{"parse worker results", ERROR_INVALID_DATA};
    }

    return host_result{
        .source_path = host_path,
        .label = std::move(label),
        .scenarios = std::move(scenarios),
    };
}

void print_result(const host_result &host)
{
    std::printf("\n# %s\n", host.label.c_str());
    std::printf("%-22s %12s %12s %12s %14s %14s\n", "scenario", "elapsed(ms)", "input(bytes)", "output(bytes)",
                "input(MiB/s)", "output(MiB/s)");

    for (const auto &scenario : host.scenarios)
    {
        const double seconds = scenario.elapsed_ms / 1000.0;
        const double input_mib = static_cast<double>(scenario.input_bytes) / (1024.0 * 1024.0);
        const double output_mib = static_cast<double>(scenario.output_bytes) / (1024.0 * 1024.0);
        std::printf("%-22s %12.3f %12zu %12zu %14.2f %14.2f\n", scenario.name.c_str(), scenario.elapsed_ms,
                    scenario.input_bytes, scenario.output_bytes, input_mib / seconds, output_mib / seconds);
    }
}

void print_comparison(const host_result &baseline, const host_result &candidate)
{
    std::printf("\n# comparison: %s / %s\n", candidate.label.c_str(), baseline.label.c_str());
    std::printf("%-22s %14s %18s\n", "scenario", "time ratio", "throughput ratio");
    for (size_t i = 0; i < std::min(baseline.scenarios.size(), candidate.scenarios.size()); ++i)
    {
        const auto &base = baseline.scenarios[i];
        const auto &cand = candidate.scenarios[i];
        const double time_ratio = cand.elapsed_ms / base.elapsed_ms;
        const double base_bytes = static_cast<double>(base.input_bytes + base.output_bytes);
        const double cand_bytes = static_cast<double>(cand.input_bytes + cand.output_bytes);
        const double base_throughput = base_bytes / base.elapsed_ms;
        const double cand_throughput = cand_bytes / cand.elapsed_ms;
        std::printf("%-22s %14.3f %18.3f\n", cand.name.c_str(), time_ratio, cand_throughput / base_throughput);
    }
}

int run(int argc, wchar_t **argv)
{
    if (argc == 3 && wcscmp(argv[1], L"--worker") == 0)
    {
        print_worker_results(run_worker_bench(narrow(argv[2])));
        return 0;
    }

    if (argc != 2 && argc != 3)
    {
        std::printf("Usage:\n");
        std::printf("  conpty_bench.exe <corehost.exe>\n");
        std::printf("  conpty_bench.exe <OpenConsole.exe> <corehost.exe>\n");
        return 1;
    }

    validate_host_path(argv[1]);
    if (argc == 2)
    {
        auto result = run_host_via_worker(argv[1], label_from_path(argv[1]), 0);
        print_result(result);
        return 0;
    }

    validate_host_path(argv[2]);
    auto baseline = run_host_via_worker(argv[1], label_from_path(argv[1]), 0);
    auto candidate = run_host_via_worker(argv[2], label_from_path(argv[2]), 1);
    print_result(baseline);
    print_result(candidate);
    print_comparison(baseline, candidate);
    return 0;
}

} // namespace bench

int wmain(int argc, wchar_t **argv)
try
{
    return bench::run(argc, argv);
}
catch (const bench::win32_failure &e)
{
    std::fprintf(stderr, "%s failed: %lu\n", e.operation, e.error);
    return 1;
}
catch (const bench::hresult_failure &e)
{
    std::fprintf(stderr, "%s failed: 0x%08lx\n", e.operation, static_cast<unsigned long>(e.hr));
    return 1;
}
catch (const std::exception &e)
{
    std::fprintf(stderr, "exception: %s\n", e.what());
    return 1;
}
