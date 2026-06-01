// === tests/conpty_test_helpers.hpp ===
// ConPTY E2E test helpers. Uses conpty_static (libconpty) to create instances.
// Includes libconpty/winconpty.h for exact type compatibility.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <chrono>
#include <thread>
#include <cstring>
#include "libconpty/winconpty.h"

namespace conpty_test
{

// RAII ConPTY instance
struct conpty_instance
{
    HPCON hpc = nullptr;
    HANDLE hInput = nullptr;
    HANDLE hOutput = nullptr;

    ~conpty_instance()
    {
        if (hpc)
        {
            ConptyClosePseudoConsole(hpc);
            hpc = nullptr;
        }
        if (hInput)
        {
            ::CloseHandle(hInput);
            hInput = nullptr;
        }
        if (hOutput)
        {
            ::CloseHandle(hOutput);
            hOutput = nullptr;
        }
    }
    conpty_instance(const conpty_instance &) = delete;
    conpty_instance &operator=(const conpty_instance &) = delete;
    conpty_instance() = default;
};

// Create a ConPTY with pipe pair
inline bool create_conpty(conpty_instance &ci, COORD size = {80, 25}, DWORD flags = 0)
{
    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

    if (!::CreatePipe(&inRead, &inWrite, &sa, 0))
        return false;
    if (!::CreatePipe(&outRead, &outWrite, &sa, 0))
    {
        ::CloseHandle(inRead);
        ::CloseHandle(inWrite);
        return false;
    }

    HPCON hpc = nullptr;
    HRESULT hr = ConptyCreatePseudoConsole(size, inRead, outWrite, flags, &hpc);
    if (FAILED(hr))
    {
        DWORD gle = ::GetLastError();
        fprintf(stderr, "ConptyCreatePseudoConsole failed: HR=0x%08X GLE=%u\n", (unsigned)hr, gle);
        fprintf(stderr, "  inRead=%p outWrite=%p\n", inRead, outWrite);
        ::CloseHandle(inRead);
        ::CloseHandle(inWrite);
        ::CloseHandle(outRead);
        ::CloseHandle(outWrite);
        return false;
    }

    ::CloseHandle(inRead);
    ::CloseHandle(outWrite);

    ci.hpc = hpc;
    ci.hInput = inWrite;
    ci.hOutput = outRead;
    return true;
}

// I/O helpers
inline bool write_input(HANDLE hInput, const void *data, DWORD len)
{
    DWORD written = 0;
    if (!::WriteFile(hInput, data, len, &written, nullptr))
    {
        std::fprintf(stderr, "WriteFile(input) failed: GLE=%lu len=%lu written=%lu\n", ::GetLastError(),
                     static_cast<unsigned long>(len), static_cast<unsigned long>(written));
        return false;
    }
    if (written != len)
    {
        std::fprintf(stderr, "WriteFile(input) short write: len=%lu written=%lu\n", static_cast<unsigned long>(len),
                     static_cast<unsigned long>(written));
        return false;
    }
    return true;
}

inline bool write_input_string(HANDLE hInput, std::string_view sv)
{
    return write_input(hInput, sv.data(), (DWORD)sv.size());
}

inline DWORD read_output(HANDLE hOutput, BYTE *buf, DWORD buf_size, DWORD timeout_ms = 2000)
{
    DWORD total = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (total < buf_size)
    {
        DWORD avail = 0;
        if (!::PeekNamedPipe(hOutput, nullptr, 0, nullptr, &avail, nullptr))
            return (total > 0) ? total : (DWORD)-1;
        if (avail > 0)
        {
            DWORD to_read = (avail < buf_size - total) ? avail : (buf_size - total);
            DWORD ck = 0;
            if (!::ReadFile(hOutput, buf + total, to_read, &ck, nullptr))
                return (total > 0) ? total : (DWORD)-1;
            total += ck;
            if (total >= buf_size)
                break;
        }
        else
        {
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return total;
}

inline std::string read_output_string(HANDLE hOutput, DWORD timeout_ms = 2000)
{
    BYTE buf[65536];
    DWORD n = read_output(hOutput, buf, sizeof(buf), timeout_ms);
    if (n == 0 || n == (DWORD)-1)
        return {};
    return std::string{(char *)buf, n};
}

inline bool expect_output_contains(HANDLE hOutput, std::string_view expected, DWORD timeout_ms = 2000)
{
    auto actual = read_output_string(hOutput, timeout_ms);
    return actual.find(expected) != std::string::npos;
}

inline void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline bool send_vt_sequence(HANDLE hInput, std::string_view vt_bytes)
{
    return write_input_string(hInput, vt_bytes);
}

inline void drain_output(HANDLE hOutput)
{
    BYTE buf[4096];
    DWORD avail = 0;
    while (::PeekNamedPipe(hOutput, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
    {
        DWORD ck = 0;
        ::ReadFile(hOutput, buf, sizeof(buf), &ck, nullptr);
    }
}

} // namespace conpty_test
