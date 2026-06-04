#pragma once

#include <cassert>
#include <cstdlib>
#include <windows.h>
#include <errhandlingapi.h>
#include <winerror.h>

namespace win32
{
enum class error : unsigned int
{
    success = 0,
    invalid_function = 1,
    file_not_found = 2,
    path_not_found = 3,
    access_denied = 5,
    invalid_handle = 6,
    out_of_memory = 14,
    bad_format = 11,
    invalid_data = 13,
    not_supported = 50,
    already_exists = 183,
    directory = 267,
    operation_aborted = 995,
    io_pending = 997,
    timeout = 1460,
    no_data = 232,
    pipe_not_connected = 233,
    pipe_connected = 535,
    broken_pipe = 109,
    cancelled = 1223,
    invalid_parameter = 87,
    gen_failure = 31,
    proc_not_found = 127,
    filename_exced_range = 207,
    bad_command = 22,
    invalid_state = 5023,
    envvar_not_found = 203,
    arithmetic_overflow = 534,
    class_already_exists = 1420,
    already_initialized = 1247,
    unhandled_exception = 574,
    bad_length = 24,
    invalid_window_handle = 1400,
};

inline error get_last_error() noexcept
{
    return static_cast<error>(::GetLastError());
}
[[noreturn]] inline void throw_last_error(error err = get_last_error())
{
    assert(err != error::success);
    throw err;
}

inline void throw_last_error(bool cond, error err)
{
    if (!cond)
        return;

    assert(err != error::success);
    throw_last_error(err);
}

inline void throw_last_error(bool cond)
{
    throw_last_error(cond, get_last_error());
}

inline void check_last_error(bool cond) noexcept
{
    if (!cond)
        return;
    auto err = get_last_error();
    assert(err != error::success);
    std::abort();
    (void)err;
}

} // namespace win32
