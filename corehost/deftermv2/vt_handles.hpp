#pragma once

#include <windows.h>
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

inline win32::handle valid_std_handle(DWORD std_handle_id) noexcept
{
    auto handle = ::GetStdHandle(std_handle_id);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return {};
    return win32::handle{handle};
}

inline win32::handle open_null_output()
{
    win32::handle output{::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    win32::throw_last_error(!output.valid());
    return output;
}

inline void initialize_vt_handles(win32::handle &input, win32::handle &output, win32::handle &input_keepalive)
{
    input = valid_std_handle(STD_INPUT_HANDLE);
    output = valid_std_handle(STD_OUTPUT_HANDLE);

    if (!input.valid())
    {
        if (!::CreatePipe(reinterpret_cast<PHANDLE>(input.put()), reinterpret_cast<PHANDLE>(input_keepalive.put()),
                          nullptr, 0))
            win32::throw_last_error();
        LOG("deftermv2::initialize_vt_handles: using empty input pipe read=%p keepalive=%p", input.get(),
            input_keepalive.get());
    }

    if (!output.valid())
    {
        output = open_null_output();
        LOG("deftermv2::initialize_vt_handles: using NUL output=%p", output.get());
    }
}

} // namespace deftermv2
