#pragma once
#include "windows.h"
#include <winternl.h>
#include "win32/error.hpp"
#include "win32/handle.hpp"
#include "win32/hresult.hpp"
#include "win32/string.hpp"

namespace condrv
{
// 向 ConDrv 创建客户端句柄（\Input / \Output / \Reference）
[[nodiscard]] inline win32::handle create_client_handle(win32::handle_view server, win32::wcstring_view name,
                                                        bool inheritable = false)
{
    UNICODE_STRING uname{.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .Buffer = const_cast<PWSTR>(name.data())};

    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &uname, OBJ_CASE_INSENSITIVE | (inheritable ? OBJ_INHERIT : 0ul), server.get(),
                               nullptr);

    win32::handle h;
    IO_STATUS_BLOCK iosb{};

    auto st = ::NtCreateFile(h.put(), GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, nullptr,
                             FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                             FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0ul);
    if (st < 0)
        win32::throw_hresult(win32::hresult(HRESULT_FROM_NT(st)));

    return h;
}

// 创建一个新的 ConDrv 控制台服务器。等价于 conhost 的
// DeviceHandle::CreateServerHandle。
[[nodiscard]] inline win32::handle create_server_handle()
{
    constexpr win32::wcstring_view name = L"\\Device\\ConDrv\\Server";

    UNICODE_STRING uname{.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t)),
                         .Buffer = const_cast<PWSTR>(name.data())};

    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &uname, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

    win32::handle h;
    IO_STATUS_BLOCK iosb{};

    const auto st = ::NtCreateFile(h.put(), GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, nullptr,
                                   FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   FILE_OPEN, 0, nullptr, 0ul);
    if (st < 0)
        win32::throw_hresult(win32::hresult(HRESULT_FROM_NT(st)));

    return h;
}
} // namespace condrv
