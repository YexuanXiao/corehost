#pragma once

#include <windows.h>
#include "utility/log.hpp"
#include "win32/error.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

inline win32::handle valid_std_handle(DWORD std_handle_id) noexcept
{
    // GetStdHandle 可返回 nullptr、INVALID_HANDLE_VALUE 或真实句柄。
    // 前两者都表示当前 deftermv2 进程没有可用标准句柄，调用方需要兜底。
    auto handle = ::GetStdHandle(std_handle_id);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return {};
    return win32::handle{handle};
}

inline win32::handle open_null_output()
{
    // NUL 用作没有 STDOUT 时的无害输出 sink。返回值必须是可写文件句柄；
    // 打开失败说明进程环境异常，直接抛出 Win32 错误。
    win32::handle output{::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    win32::throw_last_error(!output.valid());
    return output;
}

inline void initialize_vt_handles(win32::handle &input, win32::handle &output, win32::handle &input_keepalive)
{
    // input/output 为空表示 GetStdHandle 不可用。input_keepalive 仅在创建
    // 空输入管道时非空，用来持有写端，防止 vt_in 读端立即变成 EOF。
    input = valid_std_handle(STD_INPUT_HANDLE);
    output = valid_std_handle(STD_OUTPUT_HANDLE);

    if (!input.valid())
    {
        if (!::CreatePipe(reinterpret_cast<PHANDLE>(input.put()), reinterpret_cast<PHANDLE>(input_keepalive.put()),
                          nullptr, 0))
            win32::throw_last_error();
        LOG("STDIN unavailable; empty input pipe is expected read=%p keepalive=%p", input.get(), input_keepalive.get());
    }

    if (!output.valid())
    {
        output = open_null_output();
        LOG("STDOUT unavailable; NUL output sink is expected output=%p", output.get());
    }
}

} // namespace deftermv2
