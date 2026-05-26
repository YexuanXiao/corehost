#include <thread>
#include <windows.h>
#include <chrono>

#ifndef __cpp_lib_debugging
namespace std
{
inline void breakpoint() noexcept
{
    ::DebugBreak();
}

inline void breakpoint_if_debugging() noexcept
{
    if (::IsDebuggerPresent())
    {
        ::DebugBreak();
    }
}

inline bool is_debugger_present() noexcept
{
    return ::IsDebuggerPresent() != FALSE;
}
} // namespace std
#endif

namespace win32
{

inline void wait_for_debugger(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) noexcept
{
    auto start = std::chrono::high_resolution_clock::now();

    while (!std::is_debugger_present())
    {
        auto now = std::chrono::high_resolution_clock::now();
        if (now - start >= timeout)
        {
            std::abort();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1'000));
    }

    std::breakpoint();
}
} // namespace win32